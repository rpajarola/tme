/* host/eth/slirp/libslirp/glib.c - the parts of GLib that libslirp uses: */

/*
 * Copyright (c) 2026 Rico Pajarola
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

#include "glib.h"
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#define getpid _getpid
#endif

/* allocation never fails, as in GLib: */
static gpointer
_tme_slirp_g_check(gpointer p, gsize size)
{
  if (p == NULL && size != 0) {
    fprintf(stderr, "slirp: out of memory allocating %lu bytes\n", (unsigned long) size);
    abort();
  }
  return (p);
}

gpointer
g_malloc(gsize size)
{
  return (size == 0 ? NULL : _tme_slirp_g_check(malloc(size), size));
}

gpointer
g_malloc0(gsize size)
{
  return (size == 0 ? NULL : _tme_slirp_g_check(calloc(1, size), size));
}

gpointer
g_realloc(gpointer p, gsize size)
{
  if (size == 0) {
    free(p);
    return (NULL);
  }
  return (_tme_slirp_g_check(realloc(p, size), size));
}

void
g_free(gpointer p)
{
  free(p);
}

gchar *
g_strdup(const gchar *s)
{
  gsize len;

  if (s == NULL) {
    return (NULL);
  }
  len = strlen(s) + 1;
  return ((gchar *) memcpy(g_malloc(len), s, len));
}

gsize
g_strlcpy(gchar *dst, const gchar *src, gsize size)
{
  gsize len;

  len = strlen(src);
  if (size > 0) {
    if (len < size) {
      memcpy(dst, src, len + 1);
    }
    else {
      memcpy(dst, src, size - 1);
      dst[size - 1] = '\0';
    }
  }
  return (len);
}

gchar *
g_strstr_len(const gchar *haystack, long haystack_len, const gchar *needle)
{
  gsize needle_len;
  const gchar *p, *end;

  if (haystack_len < 0) {
    return ((gchar *) strstr(haystack, needle));
  }
  needle_len = strlen(needle);
  if (needle_len == 0) {
    return ((gchar *) haystack);
  }
  if ((gsize) haystack_len < needle_len) {
    return (NULL);
  }
  end = haystack + haystack_len - needle_len;
  for (p = haystack; p <= end && *p != '\0'; p++) {
    if (memcmp(p, needle, needle_len) == 0) {
      return ((gchar *) p);
    }
  }
  return (NULL);
}

gboolean
g_str_has_prefix(const gchar *s, const gchar *prefix)
{
  return (strncmp(s, prefix, strlen(prefix)) == 0);
}

gint
g_ascii_strcasecmp(const gchar *a, const gchar *b)
{
  int ca, cb;

  for (;; a++, b++) {
    ca = (unsigned char) *a;
    cb = (unsigned char) *b;
    if (ca >= 'A' && ca <= 'Z') {
      ca += 'a' - 'A';
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb += 'a' - 'A';
    }
    if (ca != cb || ca == '\0') {
      return (ca - cb);
    }
  }
}

gint
g_vsnprintf(gchar *s, gsize n, const gchar *format, va_list args)
{
  return (vsnprintf(s, n, format, args));
}

gint
g_snprintf(gchar *s, gsize n, const gchar *format, ...)
{
  va_list args;
  gint rc;

  va_start(args, format);
  rc = vsnprintf(s, n, format, args);
  va_end(args);
  return (rc);
}

const gchar *
g_strerror(gint errnum)
{
  return (strerror(errnum));
}

guint
g_strv_length(gchar **strv)
{
  guint n;

  for (n = 0; strv != NULL && strv[n] != NULL; n++);
  return (n);
}

void
g_strfreev(gchar **strv)
{
  guint n;

  if (strv != NULL) {
    for (n = 0; strv[n] != NULL; n++) {
      free(strv[n]);
    }
    free(strv);
  }
}

const gchar *
g_getenv(const gchar *name)
{
  return (getenv(name));
}

GString *
g_string_new(const gchar *init)
{
  GString *string;
  gsize len;

  len = (init != NULL ? strlen(init) : 0);
  string = g_new(GString, 1);
  string->allocated_len = len + 64;
  string->str = g_malloc(string->allocated_len);
  memcpy(string->str, (init != NULL ? init : ""), len + 1);
  string->len = len;
  return (string);
}

void
g_string_append_printf(GString *string, const gchar *format, ...)
{
  va_list args;
  int len;

  va_start(args, format);
  len = vsnprintf(NULL, 0, format, args);
  va_end(args);
  if (len < 0) {
    return;
  }
  if (string->len + len + 1 > string->allocated_len) {
    string->allocated_len = (string->len + len + 1) * 2;
    string->str = g_realloc(string->str, string->allocated_len);
  }
  va_start(args, format);
  vsnprintf(string->str + string->len, len + 1, format, args);
  va_end(args);
  string->len += len;
}

gchar *
g_string_free(GString *string, gboolean free_segment)
{
  gchar *str;

  str = string->str;
  free(string);
  if (free_segment) {
    free(str);
    return (NULL);
  }
  return (str);
}

void
g_error_free(GError *error)
{
  if (error != NULL) {
    free(error->message);
    free(error);
  }
}

/* random numbers, for ports and router advertisement intervals.
   this is xorshift64*: */
struct _GRand {
  guint64 state;
};

GRand *
g_rand_new(void)
{
  GRand *rand;
  guint64 seed;
#ifndef _WIN32
  int fd;
#endif

  seed = 0;
#ifndef _WIN32
  fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    if (read(fd, &seed, sizeof(seed)) != sizeof(seed)) {
      seed = 0;
    }
    close(fd);
  }
#endif
  seed ^= ((guint64) time(NULL) << 20) ^ (guint64) getpid() ^ (guint64) (size_t) &seed;
  rand = g_new(GRand, 1);
  rand->state = (seed != 0 ? seed : 0x9e3779b97f4a7c15ULL);
  return (rand);
}

void
g_rand_free(GRand *rand)
{
  free(rand);
}

gint32
g_rand_int_range(GRand *rand, gint32 begin, gint32 end)
{
  guint64 x;

  x = rand->state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  rand->state = x;
  x *= 0x2545f4914f6cdd1dULL;
  if (end <= begin) {
    return (begin);
  }
  return (begin + (gint32) ((x >> 32) % (guint64) (end - begin)));
}

/* this parses a list of debug keys, separated by colons, semicolons,
   commas or spaces, or "all": */
guint
g_parse_debug_string(const gchar *string, const GDebugKey *keys, guint nkeys)
{
  const gchar *p, *q;
  guint result, key_i;
  gsize len;

  result = 0;
  if (string == NULL) {
    return (0);
  }
  for (p = string; *p != '\0'; p = q) {
    for (q = p; *q != '\0' && strchr(":;, \t", *q) == NULL; q++);
    len = q - p;
    for (key_i = 0; key_i < nkeys; key_i++) {
      if ((len == 3 && strncmp(p, "all", 3) == 0)
	  || (strlen(keys[key_i].key) == len
	      && strncmp(p, keys[key_i].key, len) == 0)) {
	result |= keys[key_i].value;
      }
    }
    if (*q != '\0') {
      q++;
    }
  }
  return (result);
}

/* processes aren't supported: */
static void
_tme_slirp_g_no_spawn(GError **error)
{
  GError *err;

  if (error != NULL) {
    err = g_new0(GError, 1);
    err->code = ENOSYS;
    err->message = g_strdup("running commands isn't supported in tme");
    *error = err;
  }
}

gboolean
g_shell_parse_argv(const gchar *command, gint *_argc, gchar ***_argv, GError **error)
{
  _tme_slirp_g_no_spawn(error);
  return (FALSE);
}

gboolean
g_spawn_async(const gchar *wd, gchar **argv, gchar **envp, GSpawnFlags flags,
	      GSpawnChildSetupFunc setup, gpointer data, GPid *pid, GError **error)
{
  _tme_slirp_g_no_spawn(error);
  return (FALSE);
}

/* messages go to standard error.  debugging messages only when
   SLIRP_DEBUG is set: */
void
g_log_shim(int level, const gchar *format, ...)
{
  va_list args;
  const char *what;

  if (level == G_LOG_LEVEL_DEBUG && getenv("SLIRP_DEBUG") == NULL) {
    return;
  }
  what = (level == G_LOG_LEVEL_ERROR ? "error"
	  : level == G_LOG_LEVEL_CRITICAL ? "critical"
	  : level == G_LOG_LEVEL_WARNING ? "warning"
	  : "debug");
  fprintf(stderr, "slirp %s: ", what);
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputc('\n', stderr);
}
