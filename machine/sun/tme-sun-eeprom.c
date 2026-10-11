/* machine/sun/tme-sun-eeprom.c - dumps and makes Sun EEPROM contents.
   this was tme-sun-eeprom.pl and tools/tme-binary-struct.pl.in: */

/*
 * Copyright (c) 2004 Matt Fredette
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

/* this converts a Sun-3 EEPROM between its binary image and a text
   form, with one "name value" line per field.  the fields are
   described by the EEPROM definition below, one per line:

     OFFSET NAME TYPE[COUNT] [VALUE[=ALIAS]...]

   OFFSET is the field's offset in the image, in hexadecimal.  TYPE
   says how values are packed into the image, and COUNT how many are
   packed, by default one.  any VALUEs are the possible values, the
   first one being the default; a VALUE of "*" with an ALIAS accepts
   any value, and shows it as ALIAS.  a field whose name begins with
   a period isn't shown unless --all is given: */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

/* the EEPROM definition: */
static const char * const binary_struct[] = {
  "# amount of memory installed and tested, in MB:",
  "#",
  "0x014 installed-#megs generic_char_dec 8",
  "0x015 selftest-#megs generic_char_dec 0",
  "",
  "# screen resolution:",
  "#",
  "0x016 screen-resolution generic_char_hex 0x00=1152x900 0x12=1024x1024 0x13=1600x1280 0x14=1440x1440 0x15=640x480 0x16=1280x1024",
  "0x050 screen-#columns generic_char_dec 80",
  "0x051 screen-#rows generic_char_dec 34",
  "",
  "# the console device:",
  "#",
  "0x01f console-device generic_char_hex 0x00=onboard-bwtwo 0x10=ttya 0x11=ttyb 0x12=color-fb 0x20=p4-option",
  "",
  "# true if the watchdog causes a reset.",
  "#",
  "0x017 watchdog-reboot? sun_eeprom_boolean false",
  "",
  "# any boot device:",
  "#",
  "0x018 boot-device? sun_eeprom_boolean true",
  "0x019 boot-device sun_eeprom_boot_device sd(0,0,0)",
  "",
  "# any OEM banner and/or logo bitmap.",
  "#",
  "0x020 oem-banner? sun_eeprom_boolean false",
  "0x068 oem-banner generic_string_buffer80",
  "0x18f oem-logo? sun_eeprom_boolean false",
  "0x290 oem-logo generic_char_hex512",
  "",
  "# keyboard parameters.",
  "#",
  "0x01e keyboard-type generic_char_hex 0x00=sun *=other",
  "0x18d keyboard-locale generic_char_hex",
  "0x18e keyboard-id generic_char_hex",
  "0x021 keyboard-click? sun_eeprom_boolean false",
  "",
  "# the \"diagnostic\" boot device and file:",
  "#",
  "0x022 diag-device sun_eeprom_boot_device le(0,0,0)",
  "0x028 diag-file generic_string_buffer40",
  "",
  "# inverse video (white-on-black, not implemented?)",
  "#",
  "0x027 inverse-video? sun_eeprom_boolean false",
  "",
  "# default parameters for ttya and ttyb:",
  "#",
  "0x058 ttya-mode sun_eeprom_tty_mode 9600,8,n,1,-",
  "0x060 ttyb-mode sun_eeprom_tty_mode 9600,8,n,1,-",
  "",
  "# security mode and password (only on PROM revisions > 2.7.0).",
  "#",
  "0x492 security-mode generic_char_hex 0x00=none 0x01=command 0x5e=full",
  "0x493 security-password generic_string_buffer8",
  "",
  "# the 3/80 diagnostic \"switch\".",
  "#",
  "0x70b diag-switch? generic_char_hex 0x06=false 0x12=true *=max",
  "",
  "# any user-defined keymap:",
  "#",
  "0x18c .keymap? generic_char_hex 0x00=false 0x58=true",
  "0x190 .keymap-uppercase generic_char_hex128",
  "0x210 .keymap-lowercase generic_char_hex128",
  "",
  "# a short test pattern.",
  "#",
  "0x0b8 .test-pattern generic_shorteb_hex 0x55aa",
  "",
  "# \"Factory Defined\"",
  "#",
  "0x000 .testarea generic_longeb_hex",
  "0x004 .write-count generic_shorteb_dec3",
  "0x00c .checksum generic_char_hex3",
  "0x010 last-hardware-update generic_longeb_hex",
  "",
  "# make sure the EEPROM has the required length:",
  "#",
  "0x7ff .padding generic_char_hex",
  "",
  NULL
};

/* the field types: */
enum type_func {
  TYPE_INTEGRAL,
  TYPE_STRING_BUFFER,
  TYPE_SUN_EEPROM_BOOLEAN,
  TYPE_SUN_EEPROM_BOOT_DEVICE,
  TYPE_SUN_EEPROM_TTY_MODE
};

/* a packed value: */
struct packed {
  unsigned char *bytes;
  size_t len;
};

/* a field: */
struct field {
  char *name;
  unsigned long offset;
  char *type;
  unsigned long count;
  char *values;
  enum type_func func;
  int comment;
  char *value;
};

static const char *prog;
static struct field *fields;
static int fields_count;
static char **comments;
static int comments_count;

/* this prints an error and exits: */
static void
fatal_oom(void)
{
  fprintf(stderr, "%s fatal: out of memory\n", prog);
  exit(1);
}

static void *
xmalloc(size_t size)
{
  void *p;

  p = malloc(size == 0 ? 1 : size);
  if (p == NULL) {
    fatal_oom();
  }
  return (p);
}

static void *
xrealloc(void *p, size_t size)
{
  p = realloc(p, size == 0 ? 1 : size);
  if (p == NULL) {
    fatal_oom();
  }
  return (p);
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

static char *
xstrdup(const char *s)
{
  return (xstrndup(s, strlen(s)));
}

/* this appends to a malloced string: */
static char *
xstrcat(char *s, const char *t)
{
  size_t len;

  len = (s == NULL ? 0 : strlen(s));
  s = xrealloc(s, len + strlen(t) + 1);
  strcpy(s + len, t);
  return (s);
}

/* this splits a string on whitespace, like perl's split(' ', ...),
   returning the number of words: */
static int
split_ws(const char *s, char ***_words)
{
  char **words;
  int count;
  const char *word;

  words = NULL;
  count = 0;
  for (;;) {
    while (isspace((unsigned char) *s)) {
      s++;
    }
    if (*s == '\0') {
      break;
    }
    word = s;
    while (*s != '\0' && !isspace((unsigned char) *s)) {
      s++;
    }
    words = xrealloc(words, (count + 1) * sizeof(char *));
    words[count++] = xstrndup(word, s - word);
  }
  *_words = words;
  return (count);
}

/* this splits a string on commas, like perl's split(/,/, ...), which
   drops trailing empty parts, returning the number of parts: */
static int
split_commas(const char *s, char ***_parts)
{
  char **parts;
  int count;
  const char *comma;

  parts = NULL;
  count = 0;
  for (; s != NULL;) {
    comma = strchr(s, ',');
    parts = xrealloc(parts, (count + 1) * sizeof(char *));
    parts[count++] = (comma == NULL ? xstrdup(s) : xstrndup(s, comma - s));
    s = (comma == NULL ? NULL : comma + 1);
  }
  while (count > 0 && parts[count - 1][0] == '\0') {
    free(parts[--count]);
  }
  *_parts = parts;
  return (count);
}

static void
free_words(char **words, int count)
{
  int i;

  for (i = 0; i < count; i++) {
    free(words[i]);
  }
  free(words);
}

/* this splits a line into its first word and the rest, like perl's
   split(' ', $line, 2).  the rest is NULL if there is no whitespace
   after the first word: */
static void
split_ws2(const char *line, char **_first, char **_rest)
{
  const char *first;

  while (isspace((unsigned char) *line)) {
    line++;
  }
  first = line;
  while (*line != '\0' && !isspace((unsigned char) *line)) {
    line++;
  }
  *_first = xstrndup(first, line - first);
  if (*line == '\0') {
    *_rest = NULL;
    return;
  }
  while (isspace((unsigned char) *line)) {
    line++;
  }
  *_rest = xstrdup(line);
}

/* this returns nonzero if a line is a comment or blank: */
static int
line_is_comment(const char *line)
{
  const char *p;

  for (p = line; isspace((unsigned char) *p); p++);
  return (*p == '\0' || *p == '#');
}

/* this returns the size of an integral type: */
static unsigned int
integral_size(const char *type)
{
  if (strncmp(type, "generic_char_", 13) == 0) {
    return (1);
  }
  if (strncmp(type, "generic_shorteb_", 16) == 0) {
    return (2);
  }
  if (strncmp(type, "generic_longeb_", 15) == 0) {
    return (4);
  }
  fprintf(stderr, "%s fatal: unknown integral type %s\n", prog, type);
  exit(1);
}

/* this returns the possible values for a field, the first being the
   default: */
static int
type_values(const struct field *field, char ***_values)
{
  char **values;
  char *s;
  unsigned long i;

  switch (field->func) {

  case TYPE_SUN_EEPROM_BOOLEAN:
    if (field->values == NULL) {
      s = NULL;
      for (i = 0; i < field->count; i++) {
	s = xstrcat(s, (i > 0 ? ",false" : "false"));
      }
      s = xstrcat(s, " ");
      for (i = 0; i < field->count; i++) {
	s = xstrcat(s, (i > 0 ? ",true" : "true"));
      }
      i = split_ws(s, _values);
      free(s);
      return (i);
    }
    return (split_ws(field->values, _values));

  case TYPE_STRING_BUFFER:
    /* string buffers have no list of possible values: */
    values = xmalloc(sizeof(char *));
    values[0] = xstrdup("");
    *_values = values;
    return (1);

  default:
    if (field->values == NULL) {
      values = xmalloc(sizeof(char *));
      values[0] = xstrdup("");
      *_values = values;
      return (1);
    }
    return (split_ws(field->values, _values));
  }
}

/* this packs a value for a field.  it returns any bad part of the
   value, and sets the packed value, which is NULL if the value
   couldn't be packed at all: */
static char *
type_pack(const struct field *field, const char *value, struct packed *packed)
{
  char **parts;
  int parts_count, i;
  unsigned int size, j;
  unsigned long long part;
  char *bad;
  const char *p;
  unsigned long a, b, c, baud;
  char dev0, dev1;
  int n;

  if (value == NULL) {
    value = "";
  }
  bad = NULL;
  packed->bytes = NULL;
  packed->len = 0;

  switch (field->func) {

  case TYPE_INTEGRAL:
    size = integral_size(field->type);
    parts_count = split_commas(value, &parts);
    packed->len = size * field->count;
    packed->bytes = xmalloc(packed->len);
    memset(packed->bytes, 0, packed->len);
    for (i = 0; i < parts_count && (unsigned long) i < field->count; i++) {
      p = parts[i];
      if (p[0] == '0' && p[1] == 'x' && p[2] != '\0'
	  && strspn(p + 2, "0123456789abcdefABCDEF") == strlen(p + 2)) {
	part = strtoull(p + 2, NULL, 16);
      }
      else if (p[0] == '\'' && p[1] != '\0' && p[2] == '\'' && p[3] == '\0') {
	part = (unsigned char) p[1];
      }
      else if (p[0] != '\0' && strspn(p, "0123456789") == strlen(p)) {
	part = strtoull(p, NULL, 10);
      }
      else {
	free(bad);
	bad = xstrdup(p);
	part = 0;
      }
      for (j = 0; j < size; j++) {
	packed->bytes[(i * size) + j] = part >> (8 * (size - 1 - j));
      }
    }
    for (; i < parts_count; i++) {
      p = parts[i];
      if (!((p[0] == '0' && p[1] == 'x' && p[2] != '\0'
	     && strspn(p + 2, "0123456789abcdefABCDEF") == strlen(p + 2))
	    || (p[0] == '\'' && p[1] != '\0' && p[2] == '\'' && p[3] == '\0')
	    || (p[0] != '\0' && strspn(p, "0123456789") == strlen(p)))) {
	free(bad);
	bad = xstrdup(p);
      }
    }
    free_words(parts, parts_count);
    return (bad);

  case TYPE_SUN_EEPROM_BOOLEAN:
    parts_count = split_commas(value, &parts);
    packed->len = field->count;
    packed->bytes = xmalloc(packed->len);
    memset(packed->bytes, 0, packed->len);
    for (i = 0; i < parts_count; i++) {
      if (strcmp(parts[i], "true") == 0) {
	part = 0x12;
      }
      else if (strcmp(parts[i], "false") == 0) {
	part = 0x00;
      }
      else {
	free(bad);
	bad = xstrdup(parts[i]);
	part = 0;
      }
      if ((unsigned long) i < field->count) {
	packed->bytes[i] = part;
      }
    }
    free_words(parts, parts_count);
    return (bad);

  case TYPE_SUN_EEPROM_BOOT_DEVICE:

    /* a boot device looks like sd(0,0,0): */
    n = 0;
    if (sscanf(value, "%c%c(%lu,%lu,%lu)%n", &dev0, &dev1, &a, &b, &c, &n) == 5
	&& n == (int) strlen(value)
	&& islower((unsigned char) dev0)
	&& islower((unsigned char) dev1)
	&& isdigit((unsigned char) value[3])
	&& strspn(value + 3, "0123456789,)") == strlen(value + 3)) {
      packed->len = 5;
      packed->bytes = xmalloc(5);
      packed->bytes[0] = dev0;
      packed->bytes[1] = dev1;
      packed->bytes[2] = a;
      packed->bytes[3] = b;
      packed->bytes[4] = c;
      return (NULL);
    }
    return (xstrdup(value));

  case TYPE_SUN_EEPROM_TTY_MODE:

    /* a tty mode looks like 9600,8,n,1,-, where the last part is h
       for hardware flow control: */
    for (i = 0; isdigit((unsigned char) value[i]); i++);
    if (i > 0
	&& strncmp(value + i, ",8,n,1,", 7) == 0
	&& (value[i + 7] == '-' || value[i + 7] == 'h')
	&& value[i + 8] == '\0') {
      baud = strtoul(value, NULL, 10);
      packed->len = 8;
      packed->bytes = xmalloc(8);
      memset(packed->bytes, 0, 8);
      packed->bytes[0] = (baud == 9600 ? 0x00 : 0x12);
      packed->bytes[1] = baud >> 8;
      packed->bytes[2] = baud;
      packed->bytes[3] = (value[i + 7] == 'h' ? 0x00 : 0x12);
      return (NULL);
    }
    return (xstrdup(value));

  case TYPE_STRING_BUFFER:
    packed->len = strlen(value);
    if (packed->len > field->count) {
      bad = xstrdup(value);
    }
    else {
      packed->len = field->count;
    }
    packed->bytes = xmalloc(packed->len);
    memset(packed->bytes, 0, packed->len);
    memcpy(packed->bytes, value, strlen(value) < packed->len ? strlen(value) : packed->len);
    return (bad);
  }
  return (NULL);
}

/* this unpacks a value for a field from the image, given the bytes
   from its offset to the end of the image: */
static char *
type_unpack(const struct field *field, const unsigned char *bytes, size_t avail)
{
  char *value;
  char buffer[64];
  unsigned long count, i, part;
  unsigned int size, j;
  unsigned long *parts;
  int parts_count;
  int baud_set, baud, no_flow;

  value = xstrdup("");
  switch (field->func) {

  case TYPE_INTEGRAL:
    size = integral_size(field->type);
    count = field->count;
    if (count > avail / size) {
      count = avail / size;
    }
    parts = xmalloc(count * sizeof(unsigned long));
    for (i = 0; i < count; i++) {
      part = 0;
      for (j = 0; j < size; j++) {
	part = (part << 8) | bytes[(i * size) + j];
      }
      parts[i] = part;
    }

    /* trailing zero values are left out, except for a single
       value: */
    parts_count = count;
    while (parts_count > (field->count > 1 ? 0 : 1)
	   && parts[parts_count - 1] == 0) {
      parts_count--;
    }
    for (i = 0; i < (unsigned long) parts_count; i++) {
      if (strlen(field->type) >= 4
	  && strcmp(field->type + strlen(field->type) - 4, "_hex") == 0) {
	sprintf(buffer, "%s0x%0*lx", (i > 0 ? "," : ""), size * 2, parts[i]);
      }
      else {
	sprintf(buffer, "%s%lu", (i > 0 ? "," : ""), parts[i]);
      }
      value = xstrcat(value, buffer);
    }
    free(parts);
    break;

  case TYPE_SUN_EEPROM_BOOLEAN:
    count = (field->count < avail ? field->count : avail);
    for (i = 0; i < count; i++) {
      value = xstrcat(value, (i > 0 ? "," : ""));
      value = xstrcat(value, (bytes[i] == 0x00 ? "false" : "true"));
    }
    break;

  case TYPE_SUN_EEPROM_BOOT_DEVICE:
    free(value);
    value = xstrndup((const char *) bytes, (avail < 2 ? avail : 2));
    sprintf(buffer, "(%d,%d,%d)",
	    (avail > 2 ? bytes[2] : 0),
	    (avail > 3 ? bytes[3] : 0),
	    (avail > 4 ? bytes[4] : 0));
    value = xstrcat(value, buffer);
    break;

  case TYPE_SUN_EEPROM_TTY_MODE:
    baud_set = (avail > 0 ? bytes[0] : 0);
    baud = (avail > 2 ? ((bytes[1] << 8) | bytes[2]) : 0);
    no_flow = (avail > 3 ? bytes[3] : 0);
    if (!baud_set) {
      baud = 9600;
    }
    if (avail > 2 || !baud_set) {
      sprintf(buffer, "%d,8,n,1,%c", baud, (no_flow ? '-' : 'h'));
    }
    else {
      sprintf(buffer, ",8,n,1,%c", (no_flow ? '-' : 'h'));
    }
    value = xstrcat(value, buffer);
    break;

  case TYPE_STRING_BUFFER:
    free(value);
    for (i = 0; i < avail && bytes[i] != '\0'; i++);
    value = xstrndup((const char *) bytes, i);
    break;
  }
  return (value);
}

/* this parses the EEPROM definition: */
static char *
binary_struct_parse(void)
{
  char *input_default, *default_value, *p;
  char **words, **values;
  int words_count, values_count, line_i, comments_new, i;
  struct field *field;
  size_t len;

  input_default = xstrdup("");
  comments = xmalloc(sizeof(char *));
  comments[0] = xstrdup("");
  comments_count = 1;
  comments_new = 0;

  for (line_i = 0; binary_struct[line_i] != NULL; line_i++) {

    /* comments and blank lines are kept, to show with --verbose: */
    if (line_is_comment(binary_struct[line_i])) {
      if (comments_new) {
	comments = xrealloc(comments, (comments_count + 1) * sizeof(char *));
	comments[comments_count++] = xstrdup("");
	comments_new = 0;
      }
      comments[comments_count - 1] = xstrcat(comments[comments_count - 1], binary_struct[line_i]);
      comments[comments_count - 1] = xstrcat(comments[comments_count - 1], "\n");
      continue;
    }

    /* tokenize this line into the offset, name, type, and any
       values: */
    words_count = split_ws(binary_struct[line_i], &words);
    fields = xrealloc(fields, (fields_count + 1) * sizeof(*fields));
    field = &fields[fields_count];
    memset(field, 0, sizeof(*field));
    field->offset = strtoul(words[0], NULL, 16);
    field->name = xstrdup(words[1]);
    for (i = 0; i < fields_count; i++) {
      if (strcmp(fields[i].name, field->name) == 0) {
	fprintf(stderr, "%s internal error: %s multiply defined\n", prog, field->name);
	exit(1);
      }
    }

    /* the type may end in a count: */
    len = strlen(words[2]);
    while (len > 0 && isdigit((unsigned char) words[2][len - 1])) {
      len--;
    }
    if (len > 0 && len < strlen(words[2])) {
      field->type = xstrndup(words[2], len);
      field->count = strtoul(words[2] + len, NULL, 10);
    }
    else {
      field->type = xstrdup(words[2]);
      field->count = 1;
    }
    if (strncmp(field->type, "generic_char_", 13) == 0
	|| strncmp(field->type, "generic_short", 13) == 0
	|| strncmp(field->type, "generic_long", 12) == 0) {
      field->func = TYPE_INTEGRAL;
    }
    else if (strcmp(field->type, "generic_string_buffer") == 0) {
      field->func = TYPE_STRING_BUFFER;
    }
    else if (strcmp(field->type, "sun_eeprom_boolean") == 0) {
      field->func = TYPE_SUN_EEPROM_BOOLEAN;
    }
    else if (strcmp(field->type, "sun_eeprom_boot_device") == 0) {
      field->func = TYPE_SUN_EEPROM_BOOT_DEVICE;
    }
    else if (strcmp(field->type, "sun_eeprom_tty_mode") == 0) {
      field->func = TYPE_SUN_EEPROM_TTY_MODE;
    }
    else {
      fprintf(stderr, "%s internal error: unknown type %s\n", prog, field->type);
      exit(1);
    }

    /* any values are the rest of the line: */
    p = (char *) binary_struct[line_i];
    for (i = 0; i < 3; i++) {
      while (isspace((unsigned char) *p)) p++;
      while (*p != '\0' && !isspace((unsigned char) *p)) p++;
    }
    while (isspace((unsigned char) *p)) p++;
    field->values = (words_count > 3 ? xstrdup(p) : NULL);
    field->comment = comments_count - 1;
    free_words(words, words_count);
    fields_count++;

    /* the default value is the first possible value, or its alias: */
    values_count = type_values(field, &values);
    default_value = xstrdup(values_count > 0 ? values[0] : "");
    p = strrchr(default_value, '=');
    if (p != NULL && p[1] != '\0') {
      memmove(default_value, p + 1, strlen(p + 1) + 1);
    }
    free_words(values, values_count);
    input_default = xstrcat(input_default, field->name);
    input_default = xstrcat(input_default, " ");
    input_default = xstrcat(input_default, default_value);
    input_default = xstrcat(input_default, "\n");
    free(default_value);

    /* the next comment starts a new comment: */
    comments_new = 1;
  }
  return (input_default);
}

/* this returns the field for a name: */
static struct field *
field_find(const char *name)
{
  int i;

  for (i = 0; i < fields_count; i++) {
    if (strcmp(fields[i].name, name) == 0) {
      return (&fields[i]);
    }
  }
  return (NULL);
}

static void
usage_print(void)
{
  fprintf(stderr, "usage: %s [ OPTIONS ]\n", prog);
  fprintf(stderr, "where OPTIONS are:\n");
  fprintf(stderr, "  --verbose                 include comments in text output\n");
  fprintf(stderr, "  --all                     display normally hidden fields in text output\n");
  fprintf(stderr, "  --format-input=FORMAT     set the input format to FORMAT, one of: text binary\n");
  fprintf(stderr, "  --format-output=FORMAT    set the output format to FORMAT, one of: text binary c\n");
}

int
main(int argc, char **argv)
{
  int usage, verbose, all, arg_i, i, value_i, values_count, value_ok;
  const char *format_input, *format_output, *option;
  char *input_default, *input, *line, *line_end, *name, *value, *bad;
  char *value_alias, *wild_alias, **values, **aliases, *eq;
  unsigned char *image, buffer[1024];
  size_t input_len, image_len, len;
  ssize_t rc;
  struct field *field;
  struct packed *packeds, value_packed;
  int value_packed_ok;

  prog = strrchr(argv[0], '/');
  prog = (prog == NULL ? argv[0] : prog + 1);

  /* check our command line: */
  usage = 0;
  verbose = 0;
  all = 0;
  format_input = NULL;
  format_output = NULL;
  for (arg_i = 1; arg_i < argc && argv[arg_i][0] == '-'; arg_i++) {
    option = argv[arg_i];
    if (strcmp(option, "--verbose") == 0) {
      verbose++;
    }
    else if (strcmp(option, "--all") == 0) {
      all = 1;
    }
    else if (strncmp(option, "--format-input=", 15) == 0
	     && option[15] != '\0') {
      format_input = option + 15;
    }
    else if (strncmp(option, "--format-output=", 16) == 0
	     && option[16] != '\0') {
      format_output = option + 16;
    }
    else {
      if (strcmp(option, "-h") != 0
	  && strcmp(option, "--help") != 0
	  && strcmp(option, "-?") != 0) {
	fprintf(stderr, "%s error: unknown option `%s'\n", prog, option);
      }
      usage = 1;
      arg_i++;
      break;
    }
  }
  if (format_input != NULL
      && strcmp(format_input, "text") != 0
      && strcmp(format_input, "binary") != 0) {
    fprintf(stderr, "%s error: unknown input format %s\n", prog, format_input);
    usage = 1;
  }
  if (format_output != NULL
      && strcmp(format_output, "text") != 0
      && strcmp(format_output, "binary") != 0
      && strcmp(format_output, "c") != 0) {
    fprintf(stderr, "%s error: unknown output format %s\n", prog, format_output);
    usage = 1;
  }
  if (!usage && arg_i < argc) {
    fprintf(stderr, "%s error: `%s' unexpected\n", prog, argv[arg_i]);
    usage = 1;
  }
  if (usage) {
    usage_print();
    exit(1);
  }

  /* parse the EEPROM definition: */
  input_default = binary_struct_parse();

  /* if standard input is a terminal, there is no input: */
  input = NULL;
  input_len = 0;
  if (isatty(0)) {

    /* if the user specified the input format, and it's not text,
       that's an error: */
    if (format_input != NULL
	&& strcmp(format_input, "text") != 0) {
      fprintf(stderr, "%s error: the input format can't be %s when standard input is a terminal\n", prog, format_input);
      exit(1);
    }
    format_input = "text";
  }
  else {

    /* read in standard input: */
    for (;;) {
      rc = read(0, buffer, sizeof(buffer));
      if (rc < 0) {
	perror("fatal: could not read stdin");
	exit(1);
      }
      if (rc == 0) {
	break;
      }
      input = xrealloc(input, input_len + rc + 1);
      memcpy(input + input_len, buffer, rc);
      input_len += rc;
    }

    /* if we don't know if the input format is text or binary, try to
       figure it out: */
    if (format_input == NULL) {
      format_input = "text";
      for (len = 0; len < input_len; len++) {
	if ((unsigned char) input[len] <= 011
	    || ((unsigned char) input[len] >= 013
		&& (unsigned char) input[len] <= 036)) {
	  format_input = "binary";
	  break;
	}
      }
      fprintf(stderr, "%s notice: input format is %s\n", prog, format_input);
    }
  }
  if (input == NULL) {
    input = xmalloc(1);
  }
  input[input_len] = '\0';
  if (format_output == NULL) {
    format_output = (strcmp(format_input, "text") == 0 ? "binary" : "text");
    fprintf(stderr, "%s notice: output format is %s\n", prog, format_output);
  }
  if (strcmp(format_output, "text") != 0
      && (verbose || all)) {
    fprintf(stderr, "%s error: --verbose and --all don't make sense for binary output\n", prog);
    exit(1);
  }

  /* if the input is text: */
  if (strcmp(format_input, "text") == 0) {

    /* the default values come first, to provide values for any names
       that the user doesn't provide: */
    if (memchr(input, '\0', input_len) != NULL) {
      input_len = strlen(input);
    }
    line = xmalloc(strlen(input_default) + 1 + input_len + 1);
    sprintf(line, "%s\n%s", input_default, input);
    free(input);
    input = line;

    /* process the lines of the input: */
    for (line = input; line != NULL; line = line_end) {
      line_end = strchr(line, '\n');
      if (line_end != NULL) {
	*line_end++ = '\0';
      }

      /* ignore comments and blank lines: */
      if (line_is_comment(line)) {
	continue;
      }

      /* tokenize this line: */
      split_ws2(line, &name, &value);
      field = field_find(name);
      if (field == NULL) {
	fprintf(stderr, "%s error: unknown name `%s'\n", prog, name);
	exit(1);
      }

      /* save this value: */
      free(field->value);
      field->value = value;
      free(name);
    }
  }

  /* otherwise, the input is binary.  extract the values from the
     image: */
  else {
    for (i = 0; i < fields_count; i++) {
      field = &fields[i];
      field->value = type_unpack(field,
				 (unsigned char *) input + (field->offset < input_len ? field->offset : input_len),
				 (field->offset < input_len ? input_len - field->offset : 0));
    }
  }

  /* make the output: */
  image = NULL;
  image_len = 0;
  for (i = 0; i < fields_count; i++) {
    field = &fields[i];
    value = field->value;

    /* pack the possibilities and get any aliases: */
    values_count = type_values(field, &values);
    aliases = xmalloc(values_count * sizeof(char *));
    packeds = xmalloc(values_count * sizeof(struct packed));
    wild_alias = NULL;
    for (value_i = 0; value_i < values_count; value_i++) {

      /* strip any alias: */
      eq = strrchr(values[value_i], '=');
      if (eq != NULL && eq[1] != '\0') {
	aliases[value_i] = xstrdup(eq + 1);
	*eq = '\0';
      }
      else {
	aliases[value_i] = xstrdup("");
      }

      /* if this is the wildcard: */
      if (strcmp(values[value_i], "*") == 0
	  && aliases[value_i][0] != '\0') {
	wild_alias = aliases[value_i];
	packeds[value_i].bytes = NULL;
	packeds[value_i].len = 0;
      }

      /* otherwise, this value must pack: */
      else {
	bad = type_pack(field, values[value_i], &packeds[value_i]);
	if (bad != NULL || packeds[value_i].bytes == NULL) {
	  fprintf(stderr, "%s internal error: bad value for %s (%s)\n", prog, field->name, values[value_i]);
	  exit(1);
	}
      }
    }

    /* try to pack this value: */
    bad = type_pack(field, value, &value_packed);
    value_packed_ok = (bad == NULL);

    /* see if this value is on the list of possibilities, and is an
       alias or has an alias: */
    value_ok = 0;
    value_alias = "";
    for (value_i = 0; value_i < values_count; value_i++) {

      /* if this possibility has an alias, and the given value matches
	 the alias, stop now: */
      if (aliases[value_i][0] != '\0'
	  && strcmp((value == NULL ? "" : value), aliases[value_i]) == 0) {
	value_ok = 1;
	value_alias = aliases[value_i];
	free(value_packed.bytes);
	value_packed.len = packeds[value_i].len;
	value_packed.bytes = xmalloc(value_packed.len);
	memcpy(value_packed.bytes, packeds[value_i].bytes, value_packed.len);
	break;
      }

      /* if this value packed, and it matches this packed possibility,
	 remember that this value is on the list of possibilities, and
	 any alias: */
      if (value_packed_ok
	  && value_packed.bytes != NULL
	  && value_packed.len == packeds[value_i].len
	  && (packeds[value_i].bytes != NULL || value_packed.len == 0)
	  && memcmp(value_packed.bytes, packeds[value_i].bytes, value_packed.len) == 0) {
	value_ok = 1;
	value_alias = aliases[value_i];
      }
    }

    /* if there is a list of possible values: */
    if (values_count > 1) {

      /* if this value isn't one of them: */
      if (!value_ok) {

	/* if the wildcard is accepted: */
	if (wild_alias != NULL) {
	  value_alias = wild_alias;
	}

	/* otherwise, complain: */
	else {
	  fprintf(stderr, "%s error: bad value `%s' for %s, must be one of:",
		  prog, (value == NULL ? "" : value), field->name);
	  for (value_i = 0; value_i < values_count; value_i++) {
	    fprintf(stderr, " %s", (aliases[value_i][0] != '\0' ? aliases[value_i] : values[value_i]));
	  }
	  if (bad != NULL) {
	    fprintf(stderr, " (bad %s)", bad);
	  }
	  fprintf(stderr, "\n");
	  exit(1);
	}
      }
    }

    /* otherwise, there isn't a list of possible values.  if this
       value failed to pack: */
    else if (bad != NULL) {
      fprintf(stderr, "%s error: bad value `%s' for %s\n", prog, (value == NULL ? "" : value), field->name);
      exit(1);
    }

    /* if our output is text: */
    if (strcmp(format_output, "text") == 0) {

      /* display this field if it's not normally hidden, or if we're
	 displaying all fields: */
      if (field->name[0] != '.' || all) {

	/* if we're being verbose, display this field's comment: */
	if (verbose) {
	  fputs(comments[field->comment], stdout);
	  comments[field->comment][0] = '\0';
	}

	/* display the field and its alias or value: */
	printf("%s %s\n", field->name,
	       (value_alias[0] != '\0' ? value_alias : (value == NULL ? "" : value)));
      }
    }

    /* otherwise, our output is binary.  add this packed value to the
       image: */
    else if (value_packed.bytes != NULL) {
      if (image_len < field->offset + value_packed.len) {
	image = xrealloc(image, field->offset + value_packed.len);
	memset(image + image_len, 0, field->offset + value_packed.len - image_len);
	image_len = field->offset + value_packed.len;
      }
      memcpy(image + field->offset, value_packed.bytes, value_packed.len);
    }

    free(bad);
    free(value_packed.bytes);
    for (value_i = 0; value_i < values_count; value_i++) {
      free(packeds[value_i].bytes);
      free(aliases[value_i]);
    }
    free(packeds);
    free(aliases);
    free_words(values, values_count);
  }

  /* if our output is C, dump out the image as the default EEPROM
     contents for the emulator: */
  if (strcmp(format_output, "c") == 0) {
    printf("/* generated automatically by %s, do not edit! */\n\n", prog);
    printf("#include \"sun-idprom.h\"\n\n");
    printf("const unsigned char tme_sun3_eeprom_default[TME_SUN3_EEPROM_BYTES] = {");
    for (len = 0; len < image_len; len++) {
      printf("%s0x%02x", (len % 12 == 0 ? (len == 0 ? "\n  " : ",\n  ") : ", "), image[len]);
    }
    printf("\n};\n");
  }

  /* if our output is binary, dump out the image: */
  else if (strcmp(format_output, "binary") == 0) {
    if (image_len > 0 && fwrite(image, 1, image_len, stdout) != image_len) {
      perror("stdout");
      exit(1);
    }
  }
  if (fflush(stdout) != 0) {
    perror("stdout");
    exit(1);
  }
  return (0);
}
