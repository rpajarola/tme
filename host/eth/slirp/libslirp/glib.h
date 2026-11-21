/* host/eth/slirp/libslirp/glib.h - the parts of GLib that libslirp uses: */

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

/* libslirp is built into the slirp module with this instead of GLib,
   so that tme doesn't need GLib for it.  this has only what libslirp
   uses.  every function is renamed, so that it can't be confused
   with GLib's own when GLib is loaded too (as with GLib threads).

   process spawning isn't here: g_shell_parse_argv() always fails, so
   slirp_add_exec() forwards can't run commands.  tme doesn't use
   them. */

#ifndef _TME_SLIRP_GLIB_H
#define _TME_SLIRP_GLIB_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#endif

/* the platform: */
#ifdef _WIN32
#define G_OS_WIN32 1
#else
#define G_OS_UNIX 1
#endif

/* libslirp checks for some newer GLib functions: */
#define GLIB_CHECK_VERSION(major, minor, micro) (1)

/* the byte order and the pointer size: */
#define G_LITTLE_ENDIAN 1234
#define G_BIG_ENDIAN 4321
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define G_BYTE_ORDER G_BIG_ENDIAN
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define G_BYTE_ORDER G_LITTLE_ENDIAN
#elif defined(_WIN32) || defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
#define G_BYTE_ORDER G_LITTLE_ENDIAN
#else
#error "unknown byte order"
#endif
#if defined(__SIZEOF_POINTER__)
#define GLIB_SIZEOF_VOID_P __SIZEOF_POINTER__
#elif defined(_WIN64)
#define GLIB_SIZEOF_VOID_P 8
#else
#define GLIB_SIZEOF_VOID_P 4
#endif

/* types: */
typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef int gboolean;
typedef void *gpointer;
typedef const void *gconstpointer;
typedef size_t gsize;
typedef int16_t gint16;
typedef uint16_t guint16;
typedef int32_t gint32;
typedef uint32_t guint32;
typedef int64_t gint64;
typedef uint64_t guint64;
typedef gchar **GStrv;
typedef int GPid;

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

/* libslirp is built with this, and a missing GLib function would
   otherwise only be noticed when the module is linked, or loaded: */
#if defined(__GNUC__)
#pragma GCC diagnostic error "-Wimplicit-function-declaration"
#endif

/* macros: */
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#define GUINT16_TO_BE(v) ((guint16) htons((guint16) (v)))
#define GUINT16_FROM_BE(v) ((guint16) ntohs((guint16) (v)))
#define GINT16_TO_BE(v) ((gint16) htons((guint16) (v)))
#define GINT16_FROM_BE(v) ((gint16) ntohs((guint16) (v)))
#define GUINT32_TO_BE(v) ((guint32) htonl((guint32) (v)))
#define GUINT32_FROM_BE(v) ((guint32) ntohl((guint32) (v)))
#define GINT32_TO_BE(v) ((gint32) htonl((guint32) (v)))
#define GINT32_FROM_BE(v) ((gint32) ntohl((guint32) (v)))
#define G_N_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))
#if defined(__GNUC__)
#define G_LIKELY(e) (__builtin_expect(!!(e), 1))
#define G_UNLIKELY(e) (__builtin_expect(!!(e), 0))
#define G_GNUC_PRINTF(f, a) __attribute__((__format__(__printf__, f, a)))
#else
#define G_LIKELY(e) (e)
#define G_UNLIKELY(e) (e)
#define G_GNUC_PRINTF(f, a)
#endif
#define G_STATIC_ASSERT(e) _Static_assert(e, #e)

/* memory: */
#define g_malloc _tme_slirp_g_malloc
#define g_malloc0 _tme_slirp_g_malloc0
#define g_realloc _tme_slirp_g_realloc
#define g_free _tme_slirp_g_free
gpointer g_malloc(gsize);
gpointer g_malloc0(gsize);
gpointer g_realloc(gpointer, gsize);
void g_free(gpointer);
#define g_new(type, n) ((type *) g_malloc(sizeof(type) * (n)))
#define g_new0(type, n) ((type *) g_malloc0(sizeof(type) * (n)))

/* strings: */
#define g_strdup _tme_slirp_g_strdup
#define g_strlcpy _tme_slirp_g_strlcpy
#define g_strstr_len _tme_slirp_g_strstr_len
#define g_str_has_prefix _tme_slirp_g_str_has_prefix
#define g_ascii_strcasecmp _tme_slirp_g_ascii_strcasecmp
#define g_snprintf _tme_slirp_g_snprintf
#define g_vsnprintf _tme_slirp_g_vsnprintf
#define g_strerror _tme_slirp_g_strerror
#define g_strv_length _tme_slirp_g_strv_length
#define g_strfreev _tme_slirp_g_strfreev
#define g_getenv _tme_slirp_g_getenv
gchar *g_strdup(const gchar *);
gsize g_strlcpy(gchar *, const gchar *, gsize);
gchar *g_strstr_len(const gchar *, long, const gchar *);
gboolean g_str_has_prefix(const gchar *, const gchar *);
gint g_ascii_strcasecmp(const gchar *, const gchar *);
gint g_snprintf(gchar *, gsize, const gchar *, ...) G_GNUC_PRINTF(3, 4);
gint g_vsnprintf(gchar *, gsize, const gchar *, va_list);
const gchar *g_strerror(gint);
guint g_strv_length(gchar **);
void g_strfreev(gchar **);
const gchar *g_getenv(const gchar *);

/* growing strings: */
typedef struct {
  gchar *str;
  gsize len;
  gsize allocated_len;
} GString;
#define g_string_new _tme_slirp_g_string_new
#define g_string_append_printf _tme_slirp_g_string_append_printf
#define g_string_free _tme_slirp_g_string_free
GString *g_string_new(const gchar *);
void g_string_append_printf(GString *, const gchar *, ...) G_GNUC_PRINTF(2, 3);
gchar *g_string_free(GString *, gboolean);

/* errors: */
typedef struct {
  guint32 domain;
  gint code;
  gchar *message;
} GError;
#define g_error_free _tme_slirp_g_error_free
void g_error_free(GError *);

/* random numbers: */
typedef struct _GRand GRand;
#define g_rand_new _tme_slirp_g_rand_new
#define g_rand_free _tme_slirp_g_rand_free
#define g_rand_int_range _tme_slirp_g_rand_int_range
GRand *g_rand_new(void);
void g_rand_free(GRand *);
gint32 g_rand_int_range(GRand *, gint32, gint32);

/* debug keys: */
typedef struct {
  const gchar *key;
  guint value;
} GDebugKey;
#define g_parse_debug_string _tme_slirp_g_parse_debug_string
guint g_parse_debug_string(const gchar *, const GDebugKey *, guint);

/* processes, which aren't supported: */
typedef enum {
  G_SPAWN_DEFAULT = 0,
  G_SPAWN_SEARCH_PATH = 1 << 2
} GSpawnFlags;
typedef void (*GSpawnChildSetupFunc)(gpointer);
#define g_shell_parse_argv _tme_slirp_g_shell_parse_argv
#define g_spawn_async _tme_slirp_g_spawn_async
gboolean g_shell_parse_argv(const gchar *, gint *, gchar ***, GError **);
gboolean g_spawn_async(const gchar *, gchar **, gchar **, GSpawnFlags,
		       GSpawnChildSetupFunc, gpointer, GPid *, GError **);

/* misc.c defines g_spawn_async_with_fds() as a macro for its own
   wrapper of this, so this is a function, not a renaming macro: */
static inline gboolean
g_spawn_async_with_fds(const gchar *wd, gchar **argv, gchar **envp, GSpawnFlags flags,
		       GSpawnChildSetupFunc setup, gpointer data, GPid *pid,
		       gint stdin_fd, gint stdout_fd, gint stderr_fd, GError **error)
{
  return (g_spawn_async(wd, argv, envp, flags, setup, data, pid, error));
}

/* messages.  debugging messages are shown only when the SLIRP_DEBUG
   environment variable is set, and g_error() aborts: */
#define G_LOG_LEVEL_ERROR (1 << 2)
#define G_LOG_LEVEL_CRITICAL (1 << 3)
#define G_LOG_LEVEL_WARNING (1 << 4)
#define G_LOG_LEVEL_DEBUG (1 << 7)
#define g_log_shim _tme_slirp_g_log
void g_log_shim(int, const gchar *, ...) G_GNUC_PRINTF(2, 3);
#define g_debug(...) g_log_shim(G_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define g_warning(...) g_log_shim(G_LOG_LEVEL_WARNING, __VA_ARGS__)
#define g_critical(...) g_log_shim(G_LOG_LEVEL_CRITICAL, __VA_ARGS__)
#define g_error(...) do { g_log_shim(G_LOG_LEVEL_ERROR, __VA_ARGS__); abort(); } while (0)

/* assertions: */
#define g_assert(e)							\
  do {									\
    if (G_UNLIKELY(!(e))) {						\
      g_error("%s:%d: assertion failed: %s", __FILE__, __LINE__, #e);	\
    }									\
  } while (0)
#define g_assert_not_reached()						\
  do {									\
    g_error("%s:%d: should not be reached", __FILE__, __LINE__);	\
  } while (0)
#define g_return_if_fail(e)						\
  do {									\
    if (G_UNLIKELY(!(e))) {						\
      g_critical("%s: assertion '%s' failed", __func__, #e);		\
      return;								\
    }									\
  } while (0)
#define g_return_val_if_fail(e, v)					\
  do {									\
    if (G_UNLIKELY(!(e))) {						\
      g_critical("%s: assertion '%s' failed", __func__, #e);		\
      return (v);							\
    }									\
  } while (0)
#define g_warn_if_fail(e)						\
  do {									\
    if (G_UNLIKELY(!(e))) {						\
      g_warning("%s:%d: '%s' failed", __FILE__, __LINE__, #e);	\
    }									\
  } while (0)
#define g_warn_if_reached()						\
  g_warning("%s:%d: should not be reached", __FILE__, __LINE__)

#endif /* !_TME_SLIRP_GLIB_H */
