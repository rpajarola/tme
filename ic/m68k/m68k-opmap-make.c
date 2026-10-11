/* ic/m68k/m68k-opmap-make.c - compiles the complete decoding of all
   legal first-instruction-word values into the opcode map used by the
   C decoder.  this was m68k-iset-expand.pl and m68k-opmap-make.pl: */

/*
 * Copyright (c) 2002, 2003, 2005 Matt Fredette
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Matt Fredette.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* usage: m68k-opmap-make ISET-FILE CPU-NAME...

   this runs in two stages.  the first stage expands the instruction
   templates in ISET-FILE into full instruction word patterns and
   their decodings, once for each CPU.  the second stage compiles
   those into the opcode maps, written to standard output.  with -e,
   only the first stage is run, and its output written instead: */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* this is a growable string: */
struct str {
  char *s;
  size_t len;
  size_t size;
};

static const char *prog = "m68k-opmap-make";

/* this prints an error and exits: */
static void
die(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  exit(1);
}

static void *
xmalloc(size_t size)
{
  void *p;

  p = malloc(size == 0 ? 1 : size);
  if (p == NULL) {
    die("%s: out of memory\n", prog);
  }
  return (p);
}

static void *
xrealloc(void *p, size_t size)
{
  p = realloc(p, size == 0 ? 1 : size);
  if (p == NULL) {
    die("%s: out of memory\n", prog);
  }
  return (p);
}

static char *
xstrdup(const char *s)
{
  return (strcpy(xmalloc(strlen(s) + 1), s));
}

static char *
xstrndup(const char *s, size_t n)
{
  char *p;

  p = xmalloc(n + 1);
  memcpy(p, s, n);
  p[n] = '\0';
  return (p);
}

/* this appends to a string: */
static void
str_cat(struct str *str, const char *s)
{
  size_t len;

  len = strlen(s);
  if (str->len + len + 1 > str->size) {
    str->size = (str->len + len + 1) * 2;
    str->s = xrealloc(str->s, str->size);
  }
  memcpy(str->s + str->len, s, len + 1);
  str->len += len;
}

/* this appends a formatted string to a string: */
static void
str_catf(struct str *str, const char *fmt, ...)
{
  va_list ap;
  char buffer[1024];

  va_start(ap, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);
  str_cat(str, buffer);
}

/* this prepends a formatted string to a string: */
static void
str_prependf(struct str *str, const char *fmt, ...)
{
  va_list ap;
  char buffer[1024];
  struct str str_new;

  va_start(ap, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);
  memset(&str_new, 0, sizeof(str_new));
  str_cat(&str_new, buffer);
  str_cat(&str_new, (str->s == NULL ? "" : str->s));
  free(str->s);
  *str = str_new;
}

/* this resets a string: */
static void
str_reset(struct str *str)
{
  free(str->s);
  memset(str, 0, sizeof(*str));
  str_cat(str, "");
}

/* this is a list of strings: */
struct list {
  char **v;
  int n;
  int size;
};

static void
list_push(struct list *list, const char *s)
{
  if (list->n == list->size) {
    list->size = (list->size == 0 ? 16 : list->size * 2);
    list->v = xrealloc(list->v, list->size * sizeof(char *));
  }
  list->v[list->n++] = xstrdup(s);
}

static void
list_free(struct list *list)
{
  int i;

  for (i = 0; i < list->n; i++) {
    free(list->v[i]);
  }
  free(list->v);
  memset(list, 0, sizeof(*list));
}

/* this returns nonzero if s begins with one or more digits, and
   returns the length of those digits: */
static size_t
digits(const char *s)
{
  size_t n;

  for (n = 0; isdigit((unsigned char) s[n]); n++);
  return (n);
}

/* the first stage: */

/* an EA category, expanded for one operand size and one of the x
   and y fields: */
struct eax_cat {
  char *name;
  int size;
  int xy;
  struct list values;
};
static struct eax_cat *eax_cats;
static int eax_cats_count;

/* the current values of the fields, by field character: */
static char *field_values[256];

/* the current template: */
static int line;
static char template[17];
static char *fields[17];
static int fields_count;
static char *func;
static struct list args;
static int field_arg[256];

static const char * const threefield[8] = {
  "000", "001", "010", "011", "100", "101", "110", "111"
};

/* this splits a line into tokens like perl's split(/[,\s]+/, $_): */
static void
split_iset(char *buffer, struct list *tokens)
{
  char *p, *token;
  int leading;

  list_free(tokens);
  p = buffer;
  leading = (*p != '\0' && (*p == ',' || isspace((unsigned char) *p)));
  for (;;) {
    while (*p == ',' || isspace((unsigned char) *p)) {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    if (leading) {
      list_push(tokens, "");
      leading = 0;
    }
    token = p;
    while (*p != '\0' && *p != ',' && !isspace((unsigned char) *p)) {
      p++;
    }
    token = xstrndup(token, p - token);
    list_push(tokens, token);
    free(token);
  }
}

/* this finds an EA category: */
static struct eax_cat *
eax_cat_find(const char *name, int size, int xy, int create)
{
  int i;

  for (i = 0; i < eax_cats_count; i++) {
    if (eax_cats[i].size == size
	&& eax_cats[i].xy == xy
	&& strcmp(eax_cats[i].name, name) == 0) {
      return (&eax_cats[i]);
    }
  }
  if (!create) {
    return (NULL);
  }
  eax_cats = xrealloc(eax_cats, (eax_cats_count + 1) * sizeof(*eax_cats));
  memset(&eax_cats[eax_cats_count], 0, sizeof(*eax_cats));
  eax_cats[eax_cats_count].name = xstrdup(name);
  eax_cats[eax_cats_count].size = size;
  eax_cats[eax_cats_count].xy = xy;
  return (&eax_cats[eax_cats_count++]);
}

/* this defines an EA category: */
static void
eax_cat_define(const struct list *tokens)
{
  static const int sizes[4] = { 0, 8, 16, 32 };
  const char *name, *cycles, *modes;
  struct eax_cat *cat_x, *cat_y;
  char value_x[64], value_y[64], buffer[96];
  int size_i, size, mode, reg, mode_i;

  name = (tokens->n > 1 ? tokens->v[1] : "");
  cycles = (tokens->n > 2 ? tokens->v[2] : "");
  modes = (tokens->n > 3 ? tokens->v[3] : "");

  /* expand this category once for each possible operand size,
     remembering what operand each valid combination of mode and reg
     means: */
  for (size_i = 0; size_i < 4; size_i++) {
    size = sizes[size_i];
    eax_cat_find(name, size, 'x', 1);
    cat_y = eax_cat_find(name, size, 'y', 1);
    /* the second find may have moved the first: */
    cat_x = eax_cat_find(name, size, 'x', 0);
    list_free(&cat_x->values);
    list_free(&cat_y->values);
    for (mode = 0; mode < 8; mode++) {
      for (reg = 0; reg < 8; reg++) {

	/* EXCEPTION: 8-bit address register direct is always
	   illegal: */
	if (size == 8 && mode == 1) {
	  continue;
	}

	mode_i = mode + (mode < 7 ? 0 : reg);
	if (mode_i >= (int) strlen(modes) || modes[mode_i] != 'y') {
	  continue;
	}
	value_y[0] = '\0';
	if (mode == 0) {
	  /* data register direct: */
	  sprintf(value_x, "%%d%d.%d", reg, size);
	}
	else if (mode == 1) {
	  /* address register direct: */
	  sprintf(value_x, "%%a%d.%d", reg, size);
	}
	else if (mode == 7 && reg == 4) {
	  /* immediate: */
	  sprintf(value_x, "imm.%d", size);
	}
	else if (size == 0) {
	  /* unsized, effective address complex: */
	  strcpy(value_x, "eax.32");
	}
	else {
	  sprintf(value_x, "memx.%d.%s", size, cycles);
	  sprintf(value_y, "memy.%d.%s", size, cycles);
	}
	if (value_y[0] == '\0') {
	  strcpy(value_y, value_x);
	}
	sprintf(buffer, "%s%s/%s", threefield[mode], threefield[reg], value_x);
	list_push(&cat_x->values, buffer);
	sprintf(buffer, "%s%s/%s", threefield[reg], threefield[mode], value_y);
	list_push(&cat_y->values, buffer);
      }
    }
  }
}

/* this returns a field value, or the empty string if the field is
   undefined: */
static const char *
field_value(int field_char)
{
  const char *value;

  value = field_values[(unsigned char) field_char];
  return (value == NULL ? "" : value);
}

/* this sets a field value: */
static void
field_value_set(int field_char, const char *value)
{
  free(field_values[(unsigned char) field_char]);
  field_values[(unsigned char) field_char] = (value == NULL ? NULL : xstrdup(value));
}

/* this emits one complete pattern and its decoding: */
static void
expand_leaf(FILE *out, const char *pattern)
{
  struct str instruction;
  const char *func_now;
  char *arg, *value, *p;
  size_t func_len, n;
  int arg_i, field_char, reg;
  const char *xy_value;
  char buffer[128];

  memset(&instruction, 0, sizeof(instruction));
  str_cat(&instruction, "");

  /* EXCEPTION: a move of an address register to a predecrement or
     postincrement EA with that same address register, must store
     the original value of the address register.  since the
     predecrement and postincrement code in the executer updates the
     address register before the move has happened, we wrap the
     normal move function in another that gives an op1 argument that
     is the original value of the address register: */
  func_now = func;
  if (strncmp(func_now, "move_srpd.S", 11) == 0
      || strncmp(func_now, "move_srpi.S", 11) == 0) {
    func_now = "move.S";
  }
  if (strcmp(func_now, "move.S") == 0
      && strncmp(pattern + (15 - 5), "001", 3) == 0
      && strncmp(pattern + (15 - 5) + 3, pattern + (15 - 11), 3) == 0) {
    if (strncmp(pattern + (15 - 8), "100", 3) == 0) {
      func_now = "move_srpd.S";
    }
    else if (strncmp(pattern + (15 - 8), "011", 3) == 0) {
      func_now = "move_srpi.S";
    }
  }

  /* the function name: */
  func_len = strlen(func_now);
  if (func_len >= 2
      && func_now[func_len - 2] == '.'
      && (func_now[func_len - 1] == 's'
	  || func_now[func_len - 1] == 'S')) {
    if (field_values[(unsigned char) func_now[func_len - 1]] == NULL) {
      die("stdin:%d: %s with no size field?\n", line, func_now);
    }
    str_cat(&instruction, " func=");
    p = xstrndup(func_now, func_len - 2);
    str_cat(&instruction, p);
    free(p);
    str_cat(&instruction, field_value(func_now[func_len - 1]));
  }
  else {
    str_catf(&instruction, " func=%s", func_now);
  }

  /* handle the arguments: */
  for (arg_i = 0; arg_i < args.n; arg_i++) {
    arg = xstrdup(args.v[arg_i]);

    /* replace any .s with the s-field: */
    n = strlen(arg);
    if (n >= 2 && arg[n - 2] == '.' && arg[n - 1] == 's') {
      arg[n - 1] = '\0';
      p = xmalloc(n + 64);
      sprintf(p, "%s%s", arg, field_value('s'));
      free(arg);
      arg = p;
    }
    n = strlen(arg);
    if (n >= 2 && arg[n - 2] == '.' && arg[n - 1] == 'S') {
      arg[n - 1] = '\0';
      p = xmalloc(n + 64);
      sprintf(p, "%s%s", arg, field_value('S'));
      free(arg);
      arg = p;
    }

    /* an immediate argument: */
    if (strcmp(arg, "#s") == 0 || strcmp(arg, "#S") == 0) {
      p = xmalloc(64);
      sprintf(p, "#%s", field_value(arg[1]));
      free(arg);
      arg = p;
    }
    if ((arg[0] == '#'
	 && digits(arg + 1) > 0
	 && arg[1 + digits(arg + 1)] == '\0')
	|| strcmp(arg, "#16S32") == 0) {
      str_catf(&instruction, " imm_size=%s imm_operand=%d", arg + 1, arg_i);
    }

    /* an EA argument: */
    else if (arg[0] == 'x' || arg[0] == 'y') {
      field_char = arg[0];
      xy_value = field_value(field_char);
      value = xstrdup(xy_value);
      if (strncmp(value, (field_char == 'x' ? "memx." : "memy."), 5) == 0
	  && (n = digits(value + 5)) > 0
	  && value[5 + n] == '.'
	  && value[5 + n + 1] != '\0'
	  && !isspace((unsigned char) value[5 + n + 1])
	  && value[5 + n + 2] != '\0'
	  && !isspace((unsigned char) value[5 + n + 2])
	  && value[5 + n + 3] == '\0') {
	if (atoi(value + 5) == 0) {
	  die("stdin:%d: unsized ea%c memory argument %s (internal error?)\n", line, field_char, value);
	}
	if (strcmp(value + 5 + n + 1, "un") == 0) {
	  die("stdin:%d: ea%c memory argument with no cycles (internal error?)\n", line, field_char);
	}
	value[5 + n] = '\0';
	str_catf(&instruction, " ea%c_size=%s ea%c_cycles=%s",
		 field_char, value + 5,
		 field_char, value + 5 + n + 1);
      }
      else if (strncmp(value, "imm.", 4) == 0
	       && (n = digits(value + 4)) > 0
	       && value[4 + n] == '\0') {
	str_catf(&instruction, " imm_size=%s imm_operand=%d", value + 4, arg_i);
	value[0] = '\0';
      }
      else if (strcmp(value, "eax.32") == 0) {
	str_cat(&instruction, " eax_size=UNSIZED");
      }
      if (value[0] != '\0') {
	str_catf(&instruction, " op%d=%s", arg_i, value);
      }
      free(value);
    }

    /* a register argument: */
    else if (arg[0] == '%'
	     && strchr("daDA", arg[1]) != NULL
	     && arg[1] != '\0'
	     && arg[2] == '.'
	     && (n = digits(arg + 3)) > 0
	     && arg[3 + n] == '\0') {
      field_char = arg[1];
      reg = (isupper(field_char)
	     ? (field_char <= 'D' ? field_char - 'A' + 'a' : 'd')
	     : field_char);
      str_catf(&instruction, " op%d=%%%c%s%s", arg_i, reg, field_value(field_char), arg + 2);
    }

    /* a register number argument: */
    else if (arg[0] == '#'
	     && arg[1] != '\0'
	     && strchr("daD", arg[1]) != NULL) {
      str_catf(&instruction, " op%d=imm%s.32", arg_i, field_value(arg[1]));
    }

    /* a quick integer argument: */
    else if (strncmp(arg, "#q.", 3) == 0
	     && (n = digits(arg + 3)) > 0) {
      sprintf(buffer, "%.*s", (int) n, arg + 3);
      str_catf(&instruction, " op%d=imm%s.%s", arg_i, field_value('q'), buffer);
    }

    /* a hard-coded immediate argument: */
    else if (arg[0] == '#'
	     && (n = digits(arg + 1)) > 0
	     && arg[1 + n] == '.'
	     && digits(arg + 1 + n + 1) > 0) {
      str_catf(&instruction, " op%d=imm%.*s.%.*s", arg_i,
	       (int) n, arg + 1,
	       (int) digits(arg + 1 + n + 1), arg + 1 + n + 1);
    }

    /* handle a dummy argument: */
    else if (strcmp(arg, "X") == 0) {
    }

    else {
      die("stdin:%d: argument type %s unknown\n", line, arg);
    }
    free(arg);
  }

  /* we have a complete pattern and decoded instruction: */
  fprintf(out, "%s%s\n", pattern, instruction.s);
  free(instruction.s);
}

/* this recursively expands the fields of the current template,
   forming real, legal instruction patterns: */
static void
expand_fields(FILE *out, int depth, const char *pattern_prefix)
{
  struct list values;
  const char *field, *arg;
  char buffer[64];
  int field_char, i, eax_size;
  struct eax_cat *cat;
  char *eax_cat_name, *eax_arg_cycles, *p, *value, *slash;
  char pattern[17];
  size_t n, len;
  static const char * const cc_values[16] = {
    "0000/t",  "0001/f",  "0010/hi", "0011/ls",
    "0100/cc", "0101/cs", "0110/ne", "0111/eq",
    "1000/vc", "1001/vs", "1010/pl", "1011/mi",
    "1100/ge", "1101/lt", "1110/gt", "1111/le"
  };

  /* if we have a complete pattern, emit it: */
  if (depth == fields_count) {
    expand_leaf(out, pattern_prefix);
    return;
  }

  /* make the values for this field: */
  memset(&values, 0, sizeof(values));
  field = fields[depth];
  field_char = field[0];

  /* a constant field: */
  if (field_char == '0' || field_char == '1') {
    list_push(&values, field);
  }

  /* a wildcard field: */
  else if (field_char == 'w') {
    list_push(&values, "0");
    list_push(&values, "1");
  }

  /* a size field: */
  else if (field_char == 's') {
    list_push(&values, "00/8");
    list_push(&values, "01/16");
    list_push(&values, "10/32");
  }

  /* the wacko size field: */
  else if (field_char == 'S') {
    list_push(&values, "01/8");
    list_push(&values, "11/16");
    list_push(&values, "10/32");
  }

  /* an eax or eay field: */
  else if (field_char == 'x' || field_char == 'y') {

    /* take apart the argument, which must look like
       {x,y}{s,S,DIGITS}/CATEGORY[/CYCLES]: */
    arg = args.v[field_arg[field_char]];
    if (arg[0] != field_char) {
      die("stdin:%d: %s is a bad EA argument\n", line, arg);
    }
    if (arg[1] == 's' || arg[1] == 'S') {
      n = 1;
    }
    else {
      n = digits(arg + 1);
    }
    if (n == 0 || arg[1 + n] != '/') {
      die("stdin:%d: %s is a bad EA argument\n", line, arg);
    }
    p = xstrdup(arg + 1 + n + 1);
    slash = strchr(p, '/');
    eax_arg_cycles = NULL;
    if (slash != NULL) {
      *slash = '\0';
      eax_arg_cycles = slash + 1;
      len = strlen(eax_arg_cycles);
      if (len != 2
	  || isspace((unsigned char) eax_arg_cycles[0])
	  || isspace((unsigned char) eax_arg_cycles[1])) {
	die("stdin:%d: %s is a bad EA argument\n", line, arg);
      }
    }
    eax_cat_name = p;
    if (eax_cat_name[0] == '\0') {
      die("stdin:%d: %s is a bad EA argument\n", line, arg);
    }

    /* if the EA size depends on a size field, get it: */
    if (arg[1] == 's' || arg[1] == 'S') {
      if (field_values[(unsigned char) arg[1]] == NULL) {
	die("stdin:%d: %s argument with no size field?\n", line, arg);
      }
      eax_size = atoi(field_value(arg[1]));
    }
    else {
      eax_size = atoi(arg + 1);
    }

    /* get the field values: */
    cat = eax_cat_find(eax_cat_name, eax_size, field_char, 0);
    if (cat != NULL) {
      for (i = 0; i < cat->values.n; i++) {
	value = xmalloc(strlen(cat->values.v[i]) + 8);
	strcpy(value, cat->values.v[i]);

	/* if this argument is overriding the default cycles on the
	   EA category, enforce the override on the field values: */
	if (eax_arg_cycles != NULL) {
	  len = strlen(value);
	  if (len >= 3
	      && value[len - 3] == '.'
	      && (strcmp(value + len - 2, "un") == 0
		  || strcmp(value + len - 2, "ro") == 0
		  || strcmp(value + len - 2, "wo") == 0
		  || strcmp(value + len - 2, "rw") == 0)) {
	    strcpy(value + len - 2, eax_arg_cycles);
	  }
	}
	list_push(&values, value);
	free(value);
      }
    }
    free(p);
  }

  /* a register field: */
  else if (strchr("daDA", field_char) != NULL) {
    for (i = 0; i < 8; i++) {
      sprintf(buffer, "%s/%d", threefield[i], i);
      list_push(&values, buffer);
    }
  }

  /* a quick constant field: */
  else if (field_char == 'q') {
    for (i = 0; i < 8; i++) {
      sprintf(buffer, "%s/%d", threefield[i], (i == 0 ? 8 : i));
      list_push(&values, buffer);
    }
  }

  /* a condition code field: */
  else if (field_char == 'c') {
    for (i = 0; i < 16; i++) {
      list_push(&values, cc_values[i]);
    }
  }

  /* this field must have some values: */
  if (values.n == 0) {
    die("stdin:%d: %s-field has no values\n", line, field);
  }

  /* loop over the values for this field: */
  for (i = 0; i < values.n; i++) {
    value = xstrdup(values.v[i]);

    /* split the value into its pattern part and its field value: */
    slash = strchr(value, '/');
    if (slash != NULL) {
      *slash = '\0';
      p = slash + 1;
      slash = strchr(p, '/');
      if (slash != NULL) {
	*slash = '\0';
      }
    }
    else {
      p = "";
    }
    if (*p == '\0') {
      p = value;
    }

    /* form the pattern so far: */
    if (strlen(pattern_prefix) + strlen(value) >= sizeof(pattern)) {
      die("stdin:%d: pattern too long\n", line);
    }
    strcpy(pattern, pattern_prefix);
    strcat(pattern, value);

    /* set the field value, and descend: */
    if (field_char != '0' && field_char != '1') {
      field_value_set(field_char, p);
    }
    expand_fields(out, depth + 1, pattern);
    free(value);
  }

  /* this field no longer has a value: */
  if (field_char != '0' && field_char != '1') {
    field_value_set(field_char, NULL);
  }
  list_free(&values);
}

/* this expands the instruction templates for one CPU: */
static void
expand(FILE *in, FILE *out, const char *cpu_name)
{
  char buffer[1024];
  struct list tokens;
  int pass, i, field_start, field_end, field_size, arg_i;
  char field_char, field_char_next;
  char *p, *s;
  size_t len;

  memset(&tokens, 0, sizeof(tokens));
  fprintf(out, "cpu-begin %s\n", cpu_name);

  /* loop over the instruction set: */
  pass = 1;
  rewind(in);
  for (line = 1; fgets(buffer, sizeof(buffer), in) != NULL; line++) {
    len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[--len] = '\0';
    }

    /* split by spaces: */
    split_iset(buffer, &tokens);

    /* strip comments: */
    for (i = 0; i < tokens.n; i++) {
      if (tokens.v[i][0] == '!') {
	while (tokens.n > i) {
	  free(tokens.v[--tokens.n]);
	}
	break;
      }
    }

    /* skip blank lines: */
    if (tokens.n == 0) {
      continue;
    }

    /* handle the preprocessor.  this is the dumbest preprocessor
       ever: */
    if (strcmp(tokens.v[0], ".if") == 0) {
      pass = 0;
      for (i = 0; i < tokens.n; i++) {
	if (strcmp(tokens.v[i], cpu_name) == 0) {
	  pass = 1;
	}
      }
      continue;
    }
    else if (strcmp(tokens.v[0], ".endif") == 0) {
      pass = 1;
      continue;
    }
    if (!pass) {
      continue;
    }

    /* handle an EAX category: */
    if (strcmp(tokens.v[0], "eax-cat") == 0) {
      eax_cat_define(&tokens);
      continue;
    }

    /* handle a "specop" line.  some instructions have a separate
       opcode word that needs to be read and decoded specially.  we
       expand any .s suffixes on the instruction names but otherwise
       pass this line on untouched: */
    if (strcmp(tokens.v[0], "specop") == 0) {
      for (i = 0; i < tokens.n; i++) {
	s = tokens.v[i];
	len = strlen(s);
	if (i > 0) {
	  fputc(' ', out);
	}
	if (len > 2 && s[len - 2] == '.' && s[len - 1] == 's') {
	  fprintf(out, "%.*s8 %.*s16 %.*s32",
		  (int) (len - 2), s,
		  (int) (len - 2), s,
		  (int) (len - 2), s);
	}
	else {
	  fputs(s, out);
	}
      }
      fputc('\n', out);
      continue;
    }

    /* form the instruction template from the first four tokens: */
    template[0] = '\0';
    for (i = 0; i < 4 && i < tokens.n; i++) {
      if (strlen(template) + strlen(tokens.v[i]) >= sizeof(template)) {
	die("stdin:%d: bad template\n", line);
      }
      strcat(template, tokens.v[i]);
    }
    if (strlen(template) != 16) {
      die("stdin:%d: bad template\n", line);
    }

    /* the next token is the function, and the remaining tokens are
       the arguments: */
    func = (tokens.n > 4 ? tokens.v[4] : "");
    list_free(&args);
    for (i = 5; i < tokens.n; i++) {
      list_push(&args, tokens.v[i]);
    }

    /* break this template down into fields and sanity-check them.
       we work from the leftmost position in the template (the most
       significant bit, bit 15) to the rightmost position: */
    fields_count = 0;
    fields[fields_count++] = "root";
    for (field_start = 15; field_start >= 0; field_start = field_end - 1) {

      /* find the extent of this field.  a wildcard field (indicated
	 by the "?" character, which is internally rewritten to the
	 "w" character) is always a single bit wide, otherwise a
	 constant field ends right before a non-constant bit, and any
	 other field ends when it ends: */
      field_char = template[15 - field_start];
      if (field_char == '?') {
	field_char = 'w';
      }
      for (field_end = field_start; field_end >= 0; field_end--) {
	field_char_next = (field_end > 0 ? template[15 - (field_end - 1)] : '\0');
	if (field_char_next == '?') {
	  field_char_next = 'w';
	}
	if (field_char == 'w'
	    || ((field_char == '0' || field_char == '1')
		? !(field_char_next == '0' || field_char_next == '1')
		: field_char_next != field_char)) {
	  break;
	}
      }

      /* save this field: */
      field_size = field_start - field_end + 1;
      if (field_char == '0' || field_char == '1') {
	fields[fields_count++] = xstrndup(template + 15 - field_start, field_size);
      }
      else {
	p = xmalloc(2);
	p[0] = field_char;
	p[1] = '\0';
	fields[fields_count++] = p;
      }

      /* sanity check this field.  some fields must be a fixed size,
	 some fields must be used by some argument: */
      if (field_char == '0' || field_char == '1' || field_char == 'w') {
	/* constant and wildcard fields can have any size */
      }
      else if (field_char == 's' || field_char == 'S') {
	/* operand-size fields must be size two: */
	if (field_size != 2) {
	  die("stdin:%d: %c field must be size two\n", line, field_char);
	}
      }
      else if (field_char == 'x' || field_char == 'y') {
	/* effective address fields must be size six: */
	if (field_size != 6) {
	  die("stdin:%d: %c field must be size six\n", line, field_char);
	}
	/* effective address fields must be used by some argument: */
	for (arg_i = 0; arg_i < args.n; arg_i++) {
	  s = args.v[arg_i];
	  if (s[0] != field_char) {
	    continue;
	  }
	  if ((s[1] == 's' || s[1] == 'S') && s[2] == '/') {
	    break;
	  }
	  if (digits(s + 1) > 0 && s[1 + digits(s + 1)] == '/') {
	    break;
	  }
	}
	if (arg_i == args.n) {
	  die("stdin:%d: %c field not used by any argument\n", line, field_char);
	}
	field_arg[(unsigned char) field_char] = arg_i;
      }
      else if (strchr("daDAq", field_char) != NULL) {
	/* register and small-immediate fields must be size three: */
	if (field_size != 3) {
	  die("stdin:%d: %c field must be size three\n", line, field_char);
	}
      }
      else if (field_char == 'c') {
	/* condition code fields must be size four: */
	if (field_size != 4) {
	  die("stdin:%d: c field must be size four\n", line);
	}
      }
      else {
	die("stdin:%d: unknown field type %c\n", line, field_char);
      }
    }

    /* recursively go over the fields of this template, forming real,
       legal instruction patterns, and their full decoding: */
    expand_fields(out, 1, "");

    for (i = 1; i < fields_count; i++) {
      free(fields[i]);
    }
  }

  fprintf(out, "cpu-end %s\n", cpu_name);
  list_free(&tokens);
}

/* the second stage: */

/* the opcode immediates: */
static const char *
op_imm(const char *value)
{
  static const char * const names[9] = {
    "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT"
  };

  if (value[0] >= '0' && value[0] <= '8' && value[1] == '\0') {
    return (names[value[0] - '0']);
  }
  return ("");
}

/* the instruction functions, in order: */
static struct list insn_funcs;

/* the full map: */
static char map_u[] = "U";
#define MAP_U		map_u
static int map_line[65536];
static char *map_func[65536];
static char *map_op0[65536];
static char *map_op1[65536];
static char *map_eax_size[65536];
static char *map_eax_cycles[65536];
static char *map_imm_operand[65536];
static char *map_imm_size[65536];
static char *map_eay_size[65536];
static char *map_eay_cycles[65536];

/* the special operations: */
static struct list specop_funcs;
static struct list specop_specops;

/* the root initialization functions, by their submasks: */
static struct list root_init_submasks;

/* the previous CPU's root keys: */
static char *root_key_previous[1024];

/* a root initialization call: */
struct root_init_call {
  int root;
  int root_init_i;
  struct list params;
};

/* a parameter's submask: */
struct param_submask {
  char *param;
  unsigned long long hi;
  unsigned long long lo;
  char *key;
};

/* the state of the opcode map initialization being made: */
static struct str opcode_map_init;
static struct list param_to_local;
static int root_group_i_next;
static struct list root_init_group;
static int root_init_i_group;

/* this sets a map entry: */
static void
map_set(char **map, int pattern, const char *value)
{
  if (map[pattern] != NULL && map[pattern] != MAP_U) {
    free(map[pattern]);
  }
  map[pattern] = (value == MAP_U ? (char *) MAP_U : xstrdup(value));
}

/* this returns the instruction index for a function, adding it if
   needed: */
static int
insn_i_get(const char *func_name)
{
  int i;

  for (i = 0; i < insn_funcs.n; i++) {
    if (strcmp(insn_funcs.v[i], func_name) == 0) {
      return (i);
    }
  }
  list_push(&insn_funcs, func_name);
  return (insn_funcs.n - 1);
}

/* this compares two strings for qsort: */
static int
strcmp_qsort(const void *_a, const void *_b)
{
  return (strcmp(*(char * const *) _a, *(char * const *) _b));
}

/* this compares two param submasks by param for qsort: */
static int
param_submask_cmp(const void *_a, const void *_b)
{
  return (strcmp(((const struct param_submask *) _a)->param,
		 ((const struct param_submask *) _b)->param));
}

/* this flushes the current group of root inits: */
static void
root_init_group_flush(int root_init_i)
{
  int root_group_i, i;
  struct str roots;

  /* if there is only one root in this group: */
  if (root_init_group.n == 1) {
    str_catf(&opcode_map_init, "\n  /* root %s: */\n", root_init_group.v[0]);
    str_catf(&opcode_map_init, "  _tme_m68k_opcode_root_init_%d(opcodes + (%s * 64), params);\n\n",
	     root_init_i_group, root_init_group.v[0]);
  }

  /* if there are multiple roots in this group: */
  else if (root_init_group.n > 1) {

    /* emit the group: */
    memset(&roots, 0, sizeof(roots));
    str_cat(&roots, "");
    for (i = 0; i < root_init_group.n; i++) {
      str_catf(&roots, "%s%s", (i > 0 ? ", " : ""), root_init_group.v[i]);
    }
    root_group_i = root_group_i_next++;
    if (root_group_i == 0) {
      str_prependf(&opcode_map_init, "  tme_uint16_t root_i;\n");
    }
    str_prependf(&opcode_map_init, "  const tme_uint16_t root_group%d[] = {%s};\n",
		 root_group_i, roots.s);

    /* emit the group root init call: */
    str_catf(&opcode_map_init, "\n  /* roots %s: */\n", roots.s);
    str_catf(&opcode_map_init, "  for (root_i = 0; root_i < %d; root_i++) {\n", root_init_group.n);
    str_catf(&opcode_map_init, "    _tme_m68k_opcode_root_init_%d(opcodes + (root_group%d[root_i] * 64), params);\n",
	     root_init_i_group, root_group_i);
    str_cat(&opcode_map_init, "  }\n\n");
    free(roots.s);
  }

  /* initialize for the next group: */
  list_free(&root_init_group);
  root_init_i_group = root_init_i;
}

/* this finishes a CPU: */
static void
cpu_finish(const char *cpu_name, const char *cpu_name_previous)
{
  int pattern, root, sub, insn_i, op_i, param_i, i, j, k;
  const char *func_name, *op_map, *eax_size, *cycles, *specop;
  char func_buffer[256], op[256], op_size[64], imm_size[8], buffer[512];
  char *p;
  struct list params;
  struct param_submask *submasks_by_param;
  int submasks_by_param_count;
  struct list submask_keys;
  struct list submask_params;
  struct str root_key, submasks_joined, joined;
  int root_init_i, local;
  struct root_init_call *calls, *call;
  int calls_count;
  struct list params_state, root_init_params, split_params;
  int best, best_score, score;
  size_t n;

  /* sanity-check the information read in, and force all unused full
     map entries to illegal: */
  for (pattern = 65536; pattern-- > 0;) {

    /* if this is an unused map entry: */
    if (map_line[pattern] == 0) {
      map_set(map_func, pattern, "illegal");
      continue;
    }

    /* since the overwhelming majority of instructions use at most
       one effective address path (the eax path), only the size of
       the eax path operand is stored in the opcode maps.  any
       instruction that uses the eay path must do so with the same
       size as the eax path: */
    if (strcmp(map_eay_size[pattern], MAP_U) != 0) {
      func_name = map_func[pattern];

      /* if this instruction isn't using the eax path, switch to
	 using the eax path instead.  the only instruction allowed to
	 do this is move.S.  temporarily rewrite this function to a
	 special function "movenonmemtomem": */
      if (strcmp(map_eax_size[pattern], MAP_U) == 0) {
	if (strncmp(func_name, "move", 4) == 0
	    && (n = digits(func_name + 4)) > 0
	    && func_name[4 + n] == '\0') {
	  sprintf(func_buffer, "movenonmemtomem%s", func_name + 4);
	}
	else if ((strncmp(func_name, "move_srpi", 9) == 0
		  || strncmp(func_name, "move_srpd", 9) == 0)
		 && (n = digits(func_name + 9)) > 0
		 && func_name[9 + n] == '\0') {
	  sprintf(func_buffer, "%.9snonmemtomem%s", func_name, func_name + 9);
	}
	else {
	  die("%s: pattern (%s) uses the eay path\n", prog, func_name);
	}
	map_set(map_func, pattern, func_buffer);
	map_set(map_eax_size, pattern, map_eay_size[pattern]);
	map_set(map_eax_cycles, pattern, map_eay_cycles[pattern]);
	if (strncmp(map_op0[pattern], "memy", 4) == 0) {
	  map_op0[pattern][3] = 'x';
	}
	if (strncmp(map_op1[pattern], "memy", 4) == 0) {
	  map_op1[pattern][3] = 'x';
	}
      }

      /* otherwise, this instruction is using both ea paths.  the only
	 instruction allowed to do this is move.S.  temporarily
	 rewrite this function to a special function "movememtomem": */
      else {
	if (!(strncmp(func_name, "move", 4) == 0
	      && (n = digits(func_name + 4)) > 0
	      && func_name[4 + n] == '\0')) {
	  die("%s: pattern (%s) uses both ea paths\n", prog, func_name);
	}
	sprintf(func_buffer, "movememtomem%s", func_name + 4);
	map_set(map_func, pattern, func_buffer);
	if (strcmp(map_eay_size[pattern], map_eax_size[pattern]) != 0) {
	  die("%s: pattern (%s) uses both ea paths at different sizes\n", prog, func_buffer);
	}
	if (strcmp(map_eay_cycles[pattern], "wo") != 0) {
	  die("%s: pattern (%s) doesn't use eay write-only\n", prog, func_buffer);
	}
      }
    }
  }

  /* start the opcode map initialization for this CPU.  if there is a
     previous CPU, call its initialization function first: */
  str_reset(&opcode_map_init);
  if (cpu_name_previous != NULL) {
    str_catf(&opcode_map_init, "\n  tme_m68k_opcodes_init_%s(opcodes);\n", cpu_name_previous);
  }
  str_cat(&opcode_map_init, "\n");

  /* loop over the root patterns: */
  list_free(&param_to_local);
  calls = NULL;
  calls_count = 0;
  root_group_i_next = 0;
  memset(&params, 0, sizeof(params));
  memset(&submask_keys, 0, sizeof(submask_keys));
  memset(&submask_params, 0, sizeof(submask_params));
  memset(&params_state, 0, sizeof(params_state));
  memset(&root_init_params, 0, sizeof(root_init_params));
  memset(&split_params, 0, sizeof(split_params));
  memset(&root_key, 0, sizeof(root_key));
  memset(&submasks_joined, 0, sizeof(submasks_joined));
  memset(&joined, 0, sizeof(joined));
  for (root = 0; root < 1024; root++) {

    /* loop over the patterns under this root: */
    submasks_by_param = NULL;
    submasks_by_param_count = 0;
    for (sub = 0, pattern = root << 6; sub < 64; sub++, pattern++) {

      /* start the opcode parameters: */
      list_free(&params);

      /* the opcode function.  NB that a memory-to-memory move
	 generates the Y effective address after generating the
	 normal EA, and that a nonmemory-to-memory move generates
	 only the Y EA: */
      func_name = map_func[pattern];
      strcpy(func_buffer, func_name);
      if (strncmp(func_name, "movememtomem", 12) == 0
	  && (n = digits(func_name + 12)) > 0
	  && func_name[12 + n] == '\0') {
	sprintf(func_buffer, "move%s", func_name + 12);
	list_push(&params, "TME_M68K_OPCODE_EA_Y");
	list_push(&params, "TME_M68K_OPCODE_SPECOP");
      }
      else if (strncmp(func_name, "move", 4) == 0
	       && (p = strstr(func_name, "nonmemtomem")) != NULL) {

	/* perl's greedy /^(move.*)nonmemtomem(\d+)$/: */
	char *last, *q;
	last = NULL;
	for (q = strstr(func_name, "nonmemtomem"); q != NULL; q = strstr(q + 1, "nonmemtomem")) {
	  last = q;
	}
	n = digits(last + 11);
	if (n > 0 && last[11 + n] == '\0') {
	  sprintf(func_buffer, "%.*s%s", (int) (last - func_name), func_name, last + 11);
	  list_push(&params, "TME_M68K_OPCODE_EA_Y");
	}
      }
      insn_i = insn_i_get(func_buffer);
      sprintf(buffer, "TME_M68K_OPCODE_INSN(%d)", insn_i);
      {
	/* unshift: */
	struct list params_new;
	memset(&params_new, 0, sizeof(params_new));
	list_push(&params_new, buffer);
	for (i = 0; i < params.n; i++) {
	  list_push(&params_new, params.v[i]);
	}
	list_free(&params);
	params = params_new;
      }

      /* the two operands: */
      eax_size = NULL;
      cycles = NULL;
      for (op_i = 0; op_i < 2; op_i++) {
	op_map = (op_i == 0 ? map_op0[pattern] : map_op1[pattern]);
	strcpy(op, op_map);

	/* if this operand is a register: */
	if (op_map[0] == '%'
	    && (op_map[1] == 'a' || op_map[1] == 'd')
	    && op_map[2] >= '0' && op_map[2] <= '7'
	    && op_map[3] == '.'
	    && (n = digits(op_map + 4)) > 0) {
	  sprintf(op_size, "%.*s", (int) n, op_map + 4);
	  sprintf(buffer, "%c%c", toupper((unsigned char) op_map[1]), op_map[2]);
	  if (atoi(op_size) == 16) {
	    strcat(buffer, " << 1");
	  }
	  else if (atoi(op_size) == 8) {
	    strcat(buffer, " << 2");
	  }
	  sprintf(op, "tme_m68k_ireg_uint%s(TME_M68K_IREG_%s)", op_size, buffer);
	}

	/* if this operand is the effective address: */
	else if (strcmp(op_map, "eax.32") == 0) {
	  strcpy(op, "_tme_m68k_ea_address");
	  eax_size = map_eax_size[pattern];
	  cycles = map_eax_cycles[pattern];
	  if (strcmp(eax_size, "UNSIZED") != 0) {
	    die("%s: pattern (%s) has a sized control EA\n", prog, map_func[pattern]);
	  }
	  if (strcmp(cycles, MAP_U) != 0 && strcmp(cycles, "un") != 0) {
	    die("%s: pattern (%s) has cycles (%s) on a control EA\n", prog, map_func[pattern], cycles);
	  }
	}

	/* if this operand is an opcode immediate: */
	else if (strncmp(op_map, "imm", 3) == 0
		 && (n = digits(op_map + 3)) > 0
		 && op_map[3 + n] == '.'
		 && digits(op_map + 3 + n + 1) > 0) {
	  char value[64];
	  sprintf(value, "%.*s", (int) n, op_map + 3);
	  sprintf(op_size, "%.*s", (int) digits(op_map + 3 + n + 1), op_map + 3 + n + 1);
	  strcpy(buffer, op_imm(value));
	  if (atoi(op_size) == 16) {
	    strcat(buffer, " << 1");
	  }
	  else if (atoi(op_size) == 8) {
	    strcat(buffer, " << 2");
	  }
	  sprintf(op, "tme_m68k_ireg_uint%s(TME_M68K_IREG_%s)", op_size, buffer);
	}

	/* if this operand is a memory buffer: */
	else if (strncmp(op_map, "mem", 3) == 0
		 && (op_map[3] == 'x' || op_map[3] == 'y')
		 && op_map[4] == '.'
		 && (n = digits(op_map + 5)) > 0) {
	  sprintf(op, "tme_m68k_ireg_mem%c%.*s", op_map[3], (int) n, op_map + 5);
	  eax_size = map_eax_size[pattern];
	  cycles = map_eax_cycles[pattern];
	  if (strcmp(eax_size, "UNSIZED") == 0) {
	    die("%s: pattern (%s) has an unsized EA\n", prog, map_func[pattern]);
	  }
	  if (strcmp(cycles, MAP_U) == 0 || strcmp(cycles, "un") == 0) {
	    die("%s: pattern (%s) has no cycles on an EA\n", prog, map_func[pattern]);
	  }
	}

	else if (strcmp(op_map, MAP_U) != 0) {
	  die("%s: pattern (%s) op%d unknown: %s\n", prog, map_func[pattern], op_i, op_map);
	}

	/* if this operand is an immediate: */
	sprintf(buffer, "%d", op_i);
	if (strcmp(map_imm_operand[pattern], buffer) == 0) {
	  if (strcmp(op, MAP_U) != 0) {
	    die("%s: pattern (%s) op%d is already %s\n", prog, map_func[pattern], op_i, op);
	  }
	  strcpy(op_size, map_imm_size[pattern]);
	  strcpy(buffer, "TME_M68K_IREG_IMM32");
	  strcpy(imm_size, "16");
	  if (strcmp(op_size, "8") == 0) {
	    strcat(buffer, " << 2");
	  }
	  else if (strcmp(op_size, "16") == 0) {
	    strcat(buffer, " << 1");
	  }
	  else if (strcmp(op_size, "16S32") == 0) {
	    strcpy(op_size, "32");
	  }
	  else if (strcmp(op_size, "32") == 0) {
	    strcpy(imm_size, "32");
	  }
	  else {
	    die("%s: pattern (%s) has bad immediate size %s\n", prog, map_func[pattern], op_size);
	  }
	  sprintf(op, "tme_m68k_ireg_uint%s(%s)", op_size, buffer);
	  sprintf(buffer, "TME_M68K_OPCODE_IMM_%s", imm_size);
	  list_push(&params, buffer);
	}

	/* if this operand is defined: */
	if (strcmp(op, MAP_U) != 0) {
	  sprintf(buffer, "TME_M68K_OPCODE_OP%d(%s)", op_i, op);
	  list_push(&params, buffer);
	}
      }

      /* any EA operand: */
      if (eax_size != NULL && strcmp(eax_size, MAP_U) != 0) {
	if (strcmp(eax_size, "UNSIZED") == 0) {
	  if (strcmp(cycles, "un") != 0 && strcmp(cycles, MAP_U) != 0) {
	    die("%s: pattern (%s) has an unsized EA with %s cycles\n", prog, map_func[pattern], cycles);
	  }
	  list_push(&params, "TME_M68K_OPCODE_EA_UNSIZED");
	}
	else if (strcmp(eax_size, "8") == 0
		 || strcmp(eax_size, "16") == 0
		 || strcmp(eax_size, "32") == 0) {
	  sprintf(buffer, "TME_M68K_OPCODE_EA_SIZE(TME_M68K_SIZE_%s)", eax_size);
	  list_push(&params, buffer);
	}
	else {
	  die("%s: pattern (%s) has an %s sized EA\n", prog, map_func[pattern], eax_size);
	}
	if (strcmp(cycles, MAP_U) == 0 || strcmp(cycles, "un") == 0) {
	  /* nothing to do */
	}
	else if (strcmp(cycles, "ro") == 0) {
	  list_push(&params, "TME_M68K_OPCODE_EA_READ");
	}
	else if (strcmp(cycles, "rw") == 0) {
	  list_push(&params, "TME_M68K_OPCODE_EA_READ");
	  list_push(&params, "TME_M68K_OPCODE_EA_WRITE");
	}
	else if (strcmp(cycles, "wo") == 0) {
	  list_push(&params, "TME_M68K_OPCODE_EA_WRITE");
	}
	else {
	  die("%s: pattern (%s) has bad EA cycles\n", prog, map_func[pattern]);
	}
      }

      /* any opcode specop: */
      specop = NULL;
      for (i = 0; i < specop_funcs.n; i++) {
	if (strcmp(specop_funcs.v[i], func_buffer) == 0) {
	  specop = specop_specops.v[i];
	}
      }
      if (specop != NULL) {
	if (strcmp(specop, "specop16") != 0 && strcmp(specop, "fpgen") != 0) {
	  die("%s: pattern (%s) has a bad specop\n", prog, map_func[pattern]);
	}
	list_push(&params, "TME_M68K_OPCODE_SPECOP");
      }

      /* each param has an associated submask - a 64-bit mask with a
	 set bit for each sub in this root that needs that param.  for
	 each param used by this pattern, set this sub's bit in the
	 param's submask: */
      for (i = 0; i < params.n; i++) {
	for (j = 0; j < submasks_by_param_count; j++) {
	  if (strcmp(submasks_by_param[j].param, params.v[i]) == 0) {
	    break;
	  }
	}
	if (j == submasks_by_param_count) {
	  submasks_by_param = xrealloc(submasks_by_param,
				       (submasks_by_param_count + 1) * sizeof(*submasks_by_param));
	  submasks_by_param[j].param = xstrdup(params.v[i]);
	  submasks_by_param[j].hi = 0;
	  submasks_by_param[j].lo = 0;
	  submasks_by_param[j].key = NULL;
	  submasks_by_param_count++;
	}
	if (sub > 31) {
	  submasks_by_param[j].hi |= (1ULL << (sub & 31));
	}
	else {
	  submasks_by_param[j].lo |= (1ULL << sub);
	}
      }
    }

    /* within a root, params usually share submasks.  gather the sets
       of params that share submasks, indexed by submask, and then
       sort those keys to get a list of the submasks for the params
       passed to a root initialization function: */
    qsort(submasks_by_param, submasks_by_param_count, sizeof(*submasks_by_param), param_submask_cmp);
    list_free(&submask_keys);
    for (i = 0; i < submasks_by_param_count; i++) {
      sprintf(buffer, "%llu:%llu", submasks_by_param[i].hi, submasks_by_param[i].lo);
      submasks_by_param[i].key = xstrdup(buffer);
      for (j = 0; j < submask_keys.n; j++) {
	if (strcmp(submask_keys.v[j], buffer) == 0) {
	  break;
	}
      }
      if (j == submask_keys.n) {
	list_push(&submask_keys, buffer);
      }
    }
    qsort(submask_keys.v, submask_keys.n, sizeof(char *), strcmp_qsort);

    /* the params for each submask, in param order, each followed by
       a semicolon: */
    list_free(&submask_params);
    for (j = 0; j < submask_keys.n; j++) {
      str_reset(&joined);
      for (i = 0; i < submasks_by_param_count; i++) {
	if (strcmp(submasks_by_param[i].key, submask_keys.v[j]) == 0) {
	  str_catf(&joined, "%s;", submasks_by_param[i].param);
	}
      }
      list_push(&submask_params, joined.s);
    }

    /* if this root hasn't changed since the previous CPU, don't
       bother emitting anything for it - the previous CPU's
       initialization will take care of it: */
    str_reset(&root_key);
    for (j = 0; j < submask_keys.n; j++) {
      str_catf(&root_key, "%s %s %%", submask_keys.v[j], submask_params.v[j]);
    }
    for (i = 0; i < submasks_by_param_count; i++) {
      free(submasks_by_param[i].param);
      free(submasks_by_param[i].key);
    }
    free(submasks_by_param);
    if (root_key_previous[root] != NULL
	&& strcmp(root_key_previous[root], root_key.s) == 0) {
      continue;
    }
    free(root_key_previous[root]);
    root_key_previous[root] = xstrdup(root_key.s);

    /* if a root initialization function doesn't already exist for
       this submasks list: */
    str_reset(&submasks_joined);
    for (j = 0; j < submask_keys.n; j++) {
      str_catf(&submasks_joined, "%s%s", (j > 0 ? " " : ""), submask_keys.v[j]);
    }
    for (root_init_i = 0; root_init_i < root_init_submasks.n; root_init_i++) {
      if (strcmp(root_init_submasks.v[root_init_i], submasks_joined.s) == 0) {
	break;
      }
    }
    if (root_init_i == root_init_submasks.n) {

      /* create a new root initialization function: */
      list_push(&root_init_submasks, submasks_joined.s);
      printf("\n/* root init %d: */\n", root_init_i);
      printf("static void\n_tme_m68k_opcode_root_init_%d(tme_uint32_t *root, const tme_uint32_t *params)\n{\n", root_init_i);

      /* loop over the subs: */
      for (sub = 0; sub < 64; sub++) {

	/* make a list of params for this sub: */
	str_reset(&joined);
	for (param_i = 0; param_i < submask_keys.n; param_i++) {
	  unsigned long long hi, lo;
	  sscanf(submask_keys.v[param_i], "%llu:%llu", &hi, &lo);
	  if (((sub > 31) ? hi : lo) & (1ULL << (sub & 31))) {
	    str_catf(&joined, "%sparams[%d]", (joined.len > 0 ? " | " : ""), param_i);
	  }
	}

	/* add in this sub's parameters: */
	if (joined.len > 0) {
	  printf("  root[%d] = %s;\n", sub, joined.s);
	}
      }
      printf("}\n");
    }

    /* loop over the parameters for the root initialization
       function: */
    list_free(&root_init_params);
    for (param_i = 0; param_i < submask_keys.n; param_i++) {

      /* get the params that this root needs to provide for this root
	 initialization parameter, and store certain params in
	 constant locals, to try to make compilation easier: */
      str_reset(&joined);
      p = xstrdup(submask_params.v[param_i]);
      {
	char *param, *next;
	int first = 1;
	for (param = p; *param != '\0'; param = next) {
	  next = strchr(param, ';');
	  *next++ = '\0';
	  if (strncmp(param, "TME_M68K_OPCODE_OP", 18) == 0
	      && isdigit((unsigned char) param[18])) {
	    for (local = 0; local < param_to_local.n; local++) {
	      if (strcmp(param_to_local.v[local], param) == 0) {
		break;
	      }
	    }
	    if (local == param_to_local.n) {
	      list_push(&param_to_local, param);
	      str_prependf(&opcode_map_init, "  const tme_uint32_t param%d = %s;\n", local, param);
	    }
	    sprintf(buffer, "param%d", local);
	    param = buffer;
	  }
	  str_catf(&joined, "%s%s", (first ? "" : " | "), param);
	  first = 0;
	}
      }
      free(p);
      list_push(&root_init_params, joined.s);
    }

    /* assume that the best place to initialize this root is at the
       end of all current root initializations: */
    best = calls_count - 1;
    best_score = 0;

    /* loop over all current root initializations, tracking the
       values that each leaves in params[], and finding the current
       root initialization that leaves params[] closest to what this
       root initialization needs: */
    list_free(&params_state);
    for (k = 0; k < calls_count; k++) {

      /* get this current root initialization, and update the
	 params[] state: */
      call = &calls[k];
      for (i = 0; i < call->params.n; i++) {
	if (i < params_state.n) {
	  free(params_state.v[i]);
	  params_state.v[i] = xstrdup(call->params.v[i]);
	}
	else {
	  list_push(&params_state, call->params.v[i]);
	}
      }

      /* score our closeness to this current root initialization's
	 params[]: */
      score = 0;
      for (param_i = 0;
	   param_i < params_state.n && param_i < root_init_params.n;
	   param_i++) {
	if (strcmp(params_state.v[param_i], root_init_params.v[param_i]) == 0) {
	  score++;
	}
      }

      /* update the closest current root initialization: */
      if (best_score <= score) {
	best_score = score;
	best = k;
      }
    }

    /* add this root initialization to the list: */
    calls = xrealloc(calls, (calls_count + 1) * sizeof(*calls));
    memmove(&calls[best + 2], &calls[best + 1], (calls_count - (best + 1)) * sizeof(*calls));
    call = &calls[best + 1];
    call->root = root;
    call->root_init_i = root_init_i;
    memset(&call->params, 0, sizeof(call->params));
    for (i = 0; i < root_init_params.n; i++) {
      list_push(&call->params, root_init_params.v[i]);
    }
    calls_count++;
  }

  /* make the root initialization function calls: */
  list_free(&params_state);
  list_free(&root_init_group);
  root_init_i_group = -1;
  for (k = 0; k < calls_count; k++) {

    /* get this next root: */
    call = &calls[k];

    /* if this root uses a different init function than the current
       group, flush this group and start a new one: */
    if (call->root_init_i != root_init_i_group) {
      root_init_group_flush(call->root_init_i);
    }

    /* loop over this root's params: */
    for (param_i = 0; param_i < call->params.n; param_i++) {

      /* if this param doesn't already have the right value: */
      if (param_i >= params_state.n
	  || strcmp(params_state.v[param_i], call->params.v[param_i]) != 0) {

	/* flush the current group: */
	root_init_group_flush(call->root_init_i);

	/* set the new param value: */
	str_catf(&opcode_map_init, "  params[%d] = %s;\n", param_i, call->params.v[param_i]);
	if (param_i < params_state.n) {
	  free(params_state.v[param_i]);
	  params_state.v[param_i] = xstrdup(call->params.v[param_i]);
	}
	else {
	  list_push(&params_state, call->params.v[param_i]);
	}
      }
    }

    /* add this root to the current group: */
    sprintf(buffer, "%d", call->root);
    list_push(&root_init_group, buffer);
  }

  /* flush the last group: */
  root_init_group_flush(calls_count > 0 ? calls[calls_count - 1].root_init_i : -1);

  /* define the opcode map: */
  printf("\n");
  printf("/* the %s opcode map: */\n", cpu_name);
  printf("tme_uint32_t tme_m68k_opcodes_%s[65536];\n", cpu_name);

  /* define the opcode map initialization function: */
  printf("\n");
  printf("/* the %s opcode map initialization: */\n", cpu_name);
  printf("void\ntme_m68k_opcodes_init_%s(tme_uint32_t *opcodes)\n{\n", cpu_name);
  printf("  tme_uint32_t params[64];\n");
  fputs(opcode_map_init.s, stdout);
  printf("}\n");

  for (k = 0; k < calls_count; k++) {
    list_free(&calls[k].params);
  }
  free(calls);
  list_free(&params);
  list_free(&submask_keys);
  list_free(&submask_params);
  list_free(&params_state);
  list_free(&root_init_params);
  list_free(&split_params);
  free(root_key.s);
  free(submasks_joined.s);
  free(joined.s);
}

/* this compiles the expanded patterns into the opcode maps: */
static void
compile(FILE *in)
{
  char buffer[4096];
  char *tokens[64], *p, *value;
  int tokens_count, pattern, i;
  char cpu_name[64];
  char *cpu_name_previous;
  size_t len;
  char **map;

  cpu_name_previous = NULL;

  /* emit our header: */
  printf("/* generated automatically by %s, do not edit! */\n", prog);
  printf("\n");
  printf("/* includes: */\n");
  printf("#include \"m68k-impl.h\"\n");

  /* loop over the expanded patterns: */
  rewind(in);
  for (line = 1; fgets(buffer, sizeof(buffer), in) != NULL; line++) {
    len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[--len] = '\0';
    }

    /* break the line into tokens: */
    tokens_count = 0;
    for (p = strtok(buffer, " \t\r"); p != NULL && tokens_count < 64; p = strtok(NULL, " \t\r")) {
      tokens[tokens_count++] = p;
    }
    if (tokens_count == 0) {
      die("stdin:%d %s error: don't know how to handle an empty line\n", line, prog);
    }

    /* if this is the beginning of a new CPU: */
    if (strcmp(tokens[0], "cpu-begin") == 0) {
      snprintf(cpu_name, sizeof(cpu_name), "%s", (tokens_count > 1 ? tokens[1] : ""));

      /* initialize the full map: */
      for (pattern = 65536; pattern-- > 0;) {
	map_line[pattern] = 0;
	map_set(map_op0, pattern, MAP_U);
	map_set(map_op1, pattern, MAP_U);
	map_set(map_eax_size, pattern, MAP_U);
	map_set(map_eax_cycles, pattern, MAP_U);
	map_set(map_imm_operand, pattern, MAP_U);
	map_set(map_imm_size, pattern, MAP_U);
	map_set(map_eay_size, pattern, MAP_U);
	map_set(map_eay_cycles, pattern, MAP_U);
      }

      /* initialize the special operations: */
      list_free(&specop_funcs);
      list_free(&specop_specops);
    }

    /* if this is a special-operation line: */
    else if (strcmp(tokens[0], "specop") == 0) {
      for (i = 2; i < tokens_count; i++) {
	list_push(&specop_funcs, tokens[i]);
	list_push(&specop_specops, tokens[1]);
      }
    }

    /* if this is a pattern: */
    else if (tokens[0][0] == '0' || tokens[0][0] == '1') {

      /* the first token is the pattern.  die if this pattern has
	 already appeared: */
      pattern = (int) strtol(tokens[0], NULL, 2);
      if (map_line[pattern] != 0) {

	/* to allow for an earlier, more specific iset pattern while
	   still using very general iset patterns later, simply add to
	   the function name on the earlier, more specific iset
	   pattern, the number of asterixes for the number of later
	   general iset pattern expansions that will collide with it.
	   these collisions will be ignored: */
	len = strlen(map_func[pattern]);
	if (len > 0 && map_func[pattern][len - 1] == '*') {
	  map_func[pattern][len - 1] = '\0';
	  continue;
	}
	die("stdin:%d: duplicate pattern of line %d\n", line, map_line[pattern]);
      }
      map_line[pattern] = line;

      /* fill this map entry: */
      for (i = 1; i < tokens_count; i++) {
	value = strchr(tokens[i], '=');
	if (value == NULL) {
	  continue;
	}
	*value++ = '\0';
	map = NULL;
	if (strcmp(tokens[i], "func") == 0) map = map_func;
	else if (strcmp(tokens[i], "op0") == 0) map = map_op0;
	else if (strcmp(tokens[i], "op1") == 0) map = map_op1;
	else if (strcmp(tokens[i], "eax_size") == 0) map = map_eax_size;
	else if (strcmp(tokens[i], "eax_cycles") == 0) map = map_eax_cycles;
	else if (strcmp(tokens[i], "imm_operand") == 0) map = map_imm_operand;
	else if (strcmp(tokens[i], "imm_size") == 0) map = map_imm_size;
	else if (strcmp(tokens[i], "eay_size") == 0) map = map_eay_size;
	else if (strcmp(tokens[i], "eay_cycles") == 0) map = map_eay_cycles;
	if (map != NULL) {
	  map_set(map, pattern, value);
	}
      }
      if (map_func[pattern] == NULL) {
	die("stdin:%d: no function given\n", line);
      }
    }

    /* if this is the end of a CPU: */
    else if (strcmp(tokens[0], "cpu-end") == 0) {
      cpu_finish(cpu_name, cpu_name_previous);
      free(cpu_name_previous);
      cpu_name_previous = xstrdup(cpu_name);
    }

    /* anything else is an error: */
    else {
      die("stdin:%d %s error: don't know how to handle: %s\n", line, prog, tokens[0]);
    }
  }
  free(cpu_name_previous);

  /* emit the insn array: */
  printf("\n");
  printf("/* the insn array: */\n");
  printf("const _tme_m68k_insn tme_m68k_opcode_insns[] = {\n");
  for (i = 0; i < insn_funcs.n; i++) {
    printf("  tme_m68k_%s%s\n", insn_funcs.v[i], (i + 1 < insn_funcs.n ? "," : ""));
  }
  printf("};\n\n");
}

int
main(int argc, char **argv)
{
  FILE *iset, *expanded;
  int arg_i, expand_only;

  expand_only = 0;
  arg_i = 1;
  if (arg_i < argc && strcmp(argv[arg_i], "-e") == 0) {
    expand_only = 1;
    arg_i++;
  }
  if (argc - arg_i < 2) {
    fprintf(stderr, "usage: %s [-e] ISET-FILE CPU-NAME...\n", prog);
    exit(1);
  }

  iset = fopen(argv[arg_i], "r");
  if (iset == NULL) {
    perror(argv[arg_i]);
    exit(1);
  }
  expanded = (expand_only ? stdout : tmpfile());
  if (expanded == NULL) {
    perror("tmpfile");
    exit(1);
  }
  for (arg_i++; arg_i < argc; arg_i++) {
    expand(iset, expanded, argv[arg_i]);
  }
  fclose(iset);
  if (!expand_only) {
    fflush(expanded);
    compile(expanded);
    fclose(expanded);
  }
  if (fflush(stdout) != 0 || ferror(stdout)) {
    perror("stdout");
    exit(1);
  }
  return (0);
}
