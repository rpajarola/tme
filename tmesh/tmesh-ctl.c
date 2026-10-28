/* tmesh/tmesh-ctl.c - the tmesh control socket: */

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

/* the control socket lets a program (a script, a test harness, an AI
   agent) drive tmesh with one line of JSON per request, response, and
   event.  see the "Control socket" chapter of the manual.

   tmesh --control unix:PATH or tmesh --control tcp:PORT makes tmesh
   listen.  the socket is served by a thread of its own, which with
   cooperative threads starts over from the top each time it waits,
   so it keeps all of its state here and never waits in the middle of
   a request: CPUs and consoles (see tme/debug.h) only record what
   happens, and this thread sends events every few milliseconds.  one
   client at a time.

   tmesh --ctl SOCKET ... is a client, for sending one request from a
   shell (tmesh_ctl_client()): */

#include <tme/common.h>

/* includes: */
#include <tme/threads.h>
#include <tme/debug.h>
#include "tmesh-ctl.h"
#include "cJSON.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#ifndef WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif /* !WIN32 */

/* the program name, from tmesh.c: */
extern const char *argv0;

#ifndef WIN32

/* macros: */
#define TMESH_CTL_PROTO_VERSION		(1)
#define TMESH_CTL_POLL_USEC		(5000)
#define TMESH_CTL_MAX_LINE		(1 << 20)
#define TMESH_CTL_READ_MAX		(1 << 20)
#define TMESH_CTL_READ_DEFAULT		(65536)
#define TMESH_CTL_SEND_TIMEOUT_SEC	(5)

/* an error for a response: */
struct _tmesh_ctl_error {
  const char *code;
  char message[200];
};

#define _TMESH_CTL_HANDLER(n)	static int n(cJSON *req, cJSON *result, \
					     struct _tmesh_ctl_error *err)

/* the listening socket, the client, and what the client has sent: */
static int _tmesh_ctl_listen_fd = -1;
static int _tmesh_ctl_client_fd = -1;
static char *_tmesh_ctl_unix_path;
static char *_tmesh_ctl_inbuf;
static size_t _tmesh_ctl_inlen;

/* this runs a tmesh command: */
static int (*_tmesh_ctl_command) _TME_P((const char *, char **));

/* the next breakpoint id: */
static int _tmesh_ctl_bp_next;

/* a stop that a CPU has told about, but that hasn't been handled yet: */
struct _tmesh_ctl_stop {
  int valid;
  int cpu;
  char reason[40];
  tme_uint64_t pc;
  int bp;
  tme_uint64_t insns;

  /* for run_until: */
  const char *condition;
  char match[200];
  tme_uint64_t console_offset;
};

/* the stop to tell about, and how the machine last stopped: */
static struct _tmesh_ctl_stop _tmesh_ctl_stop_pending;
static struct _tmesh_ctl_stop _tmesh_ctl_stop_last;

/* run_until: runs until one of its conditions happens.  console
   patterns, instruction counts and timeouts are checked every few
   milliseconds, and a pc is a temporary breakpoint: */
static struct {
  int active;

  struct tme_debug_console *console;
  char **patterns;
  int n_patterns;
  size_t maxlen;
  tme_uint64_t checked;

  struct tme_debug_cpu *bp_cpu;
  int bp_id;

  int has_insns;
  tme_uint64_t insns_end;

  int has_timeout;
  tme_int64_t deadline_usec;

  /* when a condition has happened: */
  const char *fired;
  char match[200];
  tme_uint64_t match_end;
} _tmesh_ctl_ru;

/* this returns the time in microseconds: */
static tme_int64_t
_tmesh_ctl_now(void)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  return ((tme_int64_t) tv.tv_sec * 1000000 + tv.tv_usec);
}

/* this returns the CPU numbered cpu_i, or NULL: */
static struct tme_debug_cpu *
_tmesh_ctl_cpu(int cpu_i)
{
  struct tme_debug_cpu *cpu;

  for (cpu = tme_debug_cpus(); cpu != NULL && cpu_i > 0; cpu = cpu->tme_debug_cpu_next) {
    cpu_i--;
  }
  return (cpu_i < 0 ? NULL : cpu);
}

/* this returns the number of a CPU: */
static int
_tmesh_ctl_cpu_index(struct tme_debug_cpu *cpu_wanted)
{
  struct tme_debug_cpu *cpu;
  int cpu_i;

  cpu_i = 0;
  for (cpu = tme_debug_cpus(); cpu != NULL && cpu != cpu_wanted; cpu = cpu->tme_debug_cpu_next) {
    cpu_i++;
  }
  return (cpu_i);
}

/* this returns the console numbered console_i, or NULL: */
static struct tme_debug_console *
_tmesh_ctl_console(int console_i)
{
  struct tme_debug_console *console;

  for (console = tme_debug_consoles(); console != NULL && console_i > 0; console = console->tme_debug_console_next) {
    console_i--;
  }
  return (console_i < 0 ? NULL : console);
}

/* this returns about how many instructions the first CPU has run: */
static tme_uint64_t
_tmesh_ctl_insns(void)
{
  struct tme_debug_cpu *cpu;

  cpu = tme_debug_cpus();
  return (cpu == NULL ? 0 : (*cpu->tme_debug_cpu_insns)(cpu));
}

/* this returns nonzero if every CPU is stopped: */
static int
_tmesh_ctl_stopped(void)
{
  struct tme_debug_cpu *cpu;

  cpu = tme_debug_cpus();
  if (cpu == NULL) {
    return (FALSE);
  }
  for (; cpu != NULL; cpu = cpu->tme_debug_cpu_next) {
    if (!cpu->tme_debug_cpu_stopped) {
      return (FALSE);
    }
  }
  return (TRUE);
}

/* this formats an address of a CPU: */
static void
_tmesh_ctl_format_addr(char *buf, size_t len, struct tme_debug_cpu *cpu, tme_uint64_t value)
{
  if (cpu == NULL || cpu->tme_debug_cpu_bits <= 32) {
    snprintf(buf, len, "0x%08lx", (unsigned long) (tme_uint32_t) value);
  }
  else {
    snprintf(buf, len, "0x%016llx", (unsigned long long) value);
  }
}

static void
_tmesh_ctl_add_addr(cJSON *o, const char *key, struct tme_debug_cpu *cpu, tme_uint64_t value)
{
  char buf[24];

  _tmesh_ctl_format_addr(buf, sizeof(buf), cpu, value);
  cJSON_AddStringToObject(o, key, buf);
}

static int
_tmesh_ctl_fail(struct _tmesh_ctl_error *err, const char *code, const char *fmt, ...)
{
  va_list ap;

  err->code = code;
  va_start(ap, fmt);
  vsnprintf(err->message, sizeof(err->message), fmt, ap);
  va_end(ap);
  return (FALSE);
}

/* an integer parameter: a JSON number, or a string such as "10" or
   "0x10".  absent means def: */
static int
_tmesh_ctl_get_int(cJSON *req, const char *key, tme_int64_t def, tme_int64_t *_value,
		   struct _tmesh_ctl_error *err)
{
  cJSON *item;
  char *end;

  item = cJSON_GetObjectItemCaseSensitive(req, key);
  *_value = def;
  if (item == NULL || cJSON_IsNull(item)) {
    return (TRUE);
  }
  if (cJSON_IsNumber(item)) {
    *_value = (tme_int64_t) item->valuedouble;
    return (TRUE);
  }
  if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
    *_value = strtoll(item->valuestring, &end, 0);
    if (*end == '\0') {
      return (TRUE);
    }
  }
  return (_tmesh_ctl_fail(err, "bad_arg", "'%s' should be an integer", key));
}

/* this gets the value of a register of a CPU, returning zero if there
   is no such register: */
static int
_tmesh_ctl_reg_value(struct tme_debug_cpu *cpu, const char *name, size_t len, tme_uint64_t *_value)
{
  struct tme_debug_reg reg;
  unsigned int reg_i;

  if (cpu == NULL) {
    return (FALSE);
  }
  for (reg_i = 0; (*cpu->tme_debug_cpu_reg)(cpu, reg_i, &reg); reg_i++) {
    if (strlen(reg.tme_debug_reg_name) == len
	&& !strncmp(reg.tme_debug_reg_name, name, len)) {
      *_value = reg.tme_debug_reg_value;
      return (TRUE);
    }
  }
  return (FALSE);
}

/* an address parameter: a JSON number, or a string with a number
   ("0xfef0000") or a register of the CPU ("pc"), optionally plus or
   minus a number ("a7+8"): */
static int
_tmesh_ctl_get_addr(cJSON *req, const char *key, struct tme_debug_cpu *cpu,
		    tme_uint64_t *_addr, struct _tmesh_ctl_error *err)
{
  cJSON *item;
  const char *s, *p;
  char *end;
  tme_uint64_t base, offset;

  item = cJSON_GetObjectItemCaseSensitive(req, key);
  if (cJSON_IsNumber(item) && item->valuedouble >= 0) {
    *_addr = (tme_uint64_t) item->valuedouble;
    return (TRUE);
  }
  if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
    s = item->valuestring;

    /* the base, a number or a register: */
    if (s[0] >= '0' && s[0] <= '9') {
      base = strtoull(s, &end, 0);
      p = end;
    }
    else {
      for (p = s; *p != '\0' && *p != '+' && *p != '-' && *p != ' '; p++);
      if (!_tmesh_ctl_reg_value(cpu, s, p - s, &base)) {
	return (_tmesh_ctl_fail(err, "bad_arg", "'%s': no register '%.*s'", key, (int) (p - s), s));
      }
    }

    /* any offset: */
    while (*p == ' ') {
      p++;
    }
    if (*p == '\0') {
      *_addr = base;
      return (TRUE);
    }
    if ((*p == '+' || *p == '-') && p[1] != '\0') {
      offset = strtoull(p + 1, &end, 0);
      if (*end == '\0') {
	*_addr = (*p == '+' ? base + offset : base - offset);
	if (cpu != NULL && cpu->tme_debug_cpu_bits <= 32) {
	  *_addr = (tme_uint32_t) *_addr;
	}
	return (TRUE);
      }
    }
  }
  return (_tmesh_ctl_fail(err, "bad_arg", "'%s' should be an address: a number, or a register plus or minus a number (\"pc+16\")", key));
}

/* the "cpu" parameter: a CPU number, by default the first CPU: */
static int
_tmesh_ctl_get_cpu(cJSON *req, struct tme_debug_cpu **_cpu, struct _tmesh_ctl_error *err)
{
  tme_int64_t cpu_i;

  if (!_tmesh_ctl_get_int(req, "cpu", 0, &cpu_i, err)) {
    return (FALSE);
  }
  *_cpu = (cpu_i >= 0 && cpu_i < 1000 ? _tmesh_ctl_cpu(cpu_i) : NULL);
  if (*_cpu == NULL) {
    return (_tmesh_ctl_fail(err, "no_such_cpu", "no cpu %lld", (long long) cpu_i));
  }
  return (TRUE);
}

/* the "console" parameter: a console number, by default the first
   console: */
static int
_tmesh_ctl_get_console(cJSON *req, struct tme_debug_console **_console, struct _tmesh_ctl_error *err)
{
  tme_int64_t console_i;

  if (!_tmesh_ctl_get_int(req, "console", 0, &console_i, err)) {
    return (FALSE);
  }
  *_console = (console_i >= 0 && console_i < 1000 ? _tmesh_ctl_console(console_i) : NULL);
  if (*_console == NULL) {
    return (_tmesh_ctl_fail(err, "no_such_console", "no console %lld (a console is a tme/host/posix/console element)", (long long) console_i));
  }
  return (TRUE);
}

/*****************************************************************************/

/* sending: */

static void
_tmesh_ctl_disconnect(void)
{
  if (_tmesh_ctl_client_fd < 0) {
    return;
  }
  close(_tmesh_ctl_client_fd);
  _tmesh_ctl_client_fd = -1;
  _tmesh_ctl_inlen = 0;
}

static int
_tmesh_ctl_send_all(int fd, const char *buf, size_t len)
{
  ssize_t rc;
  int flags;

#ifdef MSG_NOSIGNAL
  flags = MSG_NOSIGNAL;
#else
  flags = 0;
#endif
  while (len > 0) {
    rc = send(fd, buf, len, flags);
    if (rc > 0) {
      buf += rc;
      len -= rc;
      continue;
    }
    if (rc < 0 && errno == EINTR) {
      continue;
    }
    return (FALSE);
  }
  return (TRUE);
}

/* this sends a message to the client as one line, and frees it: */
static void
_tmesh_ctl_send(cJSON *msg)
{
  char *s;
  size_t len;

  if (_tmesh_ctl_client_fd >= 0) {
    s = cJSON_PrintUnformatted(msg);
    if (s != NULL) {
      len = strlen(s);
      s = realloc(s, len + 2);
      s[len] = '\n';
      s[len + 1] = '\0';
      if (!_tmesh_ctl_send_all(_tmesh_ctl_client_fd, s, len + 1)) {
	_tmesh_ctl_disconnect();
      }
      free(s);
    }
  }
  cJSON_Delete(msg);
}

static void
_tmesh_ctl_add_stop(cJSON *o, const struct _tmesh_ctl_stop *stop)
{
  struct tme_debug_cpu *cpu;

  cpu = _tmesh_ctl_cpu(stop->cpu);
  cJSON_AddStringToObject(o, "reason", stop->reason);
  if (stop->bp > 0) {
    cJSON_AddNumberToObject(o, "bp", stop->bp);
  }
  cJSON_AddNumberToObject(o, "cpu", stop->cpu);
  _tmesh_ctl_add_addr(o, "pc", cpu, stop->pc);
  if (stop->condition != NULL) {
    cJSON_AddStringToObject(o, "condition", stop->condition);
    if (!strcmp(stop->condition, "console")) {
      cJSON_AddStringToObject(o, "match", stop->match);
      cJSON_AddNumberToObject(o, "console_offset", (double) stop->console_offset);
    }
  }
}

/* a CPU calls this, on its own thread, when it stops: */
static void
_tmesh_ctl_stop_hook(struct tme_debug_cpu *cpu)
{
  struct _tmesh_ctl_stop *stop;

  /* the first stop until this thread looks is the one told about: */
  stop = &_tmesh_ctl_stop_pending;
  if (stop->valid) {
    return;
  }
  memset(stop, 0, sizeof(*stop));
  stop->cpu = _tmesh_ctl_cpu_index(cpu);
  snprintf(stop->reason, sizeof(stop->reason), "%s",
	   cpu->tme_debug_cpu_stop_reason != NULL ? cpu->tme_debug_cpu_stop_reason : "other");
  stop->pc = (*cpu->tme_debug_cpu_pc)(cpu);
  stop->bp = cpu->tme_debug_cpu_breakpoint_hit;
  stop->insns = (*cpu->tme_debug_cpu_insns)(cpu);
  stop->valid = TRUE;
}

static void _tmesh_ctl_run_until_clear _TME_P((void));

/* this handles a stop that a CPU has told about: */
static void
_tmesh_ctl_stop_handle(void)
{
  struct _tmesh_ctl_stop *stop;
  cJSON *ev;

  stop = &_tmesh_ctl_stop_pending;
  if (!stop->valid) {
    return;
  }

  /* the end of a run_until, because of one of its conditions or not: */
  if (_tmesh_ctl_ru.active) {
    if (!strcmp(stop->reason, "run_until") && _tmesh_ctl_ru.fired != NULL) {
      stop->condition = _tmesh_ctl_ru.fired;
      snprintf(stop->match, sizeof(stop->match), "%s", _tmesh_ctl_ru.match);
      stop->console_offset = _tmesh_ctl_ru.match_end;
    }
    else if (stop->bp != 0 && stop->bp == _tmesh_ctl_ru.bp_id) {
      snprintf(stop->reason, sizeof(stop->reason), "run_until");
      stop->condition = "pc";
      stop->bp = 0;
    }
    _tmesh_ctl_run_until_clear();
  }

  _tmesh_ctl_stop_last = *stop;
  stop->valid = FALSE;

  ev = cJSON_CreateObject();
  cJSON_AddStringToObject(ev, "event", "stopped");
  _tmesh_ctl_add_stop(ev, &_tmesh_ctl_stop_last);
  cJSON_AddNumberToObject(ev, "ninstrs", (double) _tmesh_ctl_stop_last.insns);
  _tmesh_ctl_send(ev);
}

/*****************************************************************************/

/* run_until: */

static void
_tmesh_ctl_run_until_clear(void)
{
  int i;

  if (_tmesh_ctl_ru.bp_id != 0) {
    (void) tme_debug_cpu_breakpoint_remove(_tmesh_ctl_ru.bp_cpu, _tmesh_ctl_ru.bp_id);
  }
  for (i = 0; i < _tmesh_ctl_ru.n_patterns; i++) {
    free(_tmesh_ctl_ru.patterns[i]);
  }
  free(_tmesh_ctl_ru.patterns);
  memset(&_tmesh_ctl_ru, 0, sizeof(_tmesh_ctl_ru));
}

/* this stops the machine if a run_until condition has happened: */
static void
_tmesh_ctl_run_until_check(void)
{
  struct tme_debug_cpu *cpu;
  tme_uint64_t from, start, total;
  tme_uint8_t *buf;
  size_t count, i, plen, best_end;
  int p, best;

  if (!_tmesh_ctl_ru.active
      || _tmesh_ctl_ru.fired != NULL
      || _tmesh_ctl_stopped()) {
    return;
  }

  if (_tmesh_ctl_ru.n_patterns > 0) {

    /* search from a little before, for patterns which began in the
       output seen last time: */
    from = _tmesh_ctl_ru.checked;
    from = (from > _tmesh_ctl_ru.maxlen - 1 ? from - (_tmesh_ctl_ru.maxlen - 1) : 0);
    (void) tme_debug_console_read(_tmesh_ctl_ru.console, ~(tme_uint64_t) 0, NULL, 0, &start, &total);
    if (total > from) {
      buf = tme_new(tme_uint8_t, total - from);
      count = tme_debug_console_read(_tmesh_ctl_ru.console, from, buf, total - from, &start, &total);

      /* the match which ends first: */
      best = -1;
      best_end = 0;
      for (p = 0; p < _tmesh_ctl_ru.n_patterns; p++) {
	plen = strlen(_tmesh_ctl_ru.patterns[p]);
	for (i = 0; i + plen <= count; i++) {
	  if (!memcmp(buf + i, _tmesh_ctl_ru.patterns[p], plen)) {
	    if (best < 0 || i + plen < best_end) {
	      best = p;
	      best_end = i + plen;
	    }
	    break;
	  }
	}
      }
      tme_free(buf);
      _tmesh_ctl_ru.checked = total;

      if (best >= 0) {
	_tmesh_ctl_ru.fired = "console";
	snprintf(_tmesh_ctl_ru.match, sizeof(_tmesh_ctl_ru.match), "%s", _tmesh_ctl_ru.patterns[best]);
	_tmesh_ctl_ru.match_end = start + best_end;
      }
    }
  }

  if (_tmesh_ctl_ru.fired == NULL
      && _tmesh_ctl_ru.has_insns
      && _tmesh_ctl_insns() >= _tmesh_ctl_ru.insns_end) {
    _tmesh_ctl_ru.fired = "insns";
  }
  if (_tmesh_ctl_ru.fired == NULL
      && _tmesh_ctl_ru.has_timeout
      && _tmesh_ctl_now() >= _tmesh_ctl_ru.deadline_usec) {
    _tmesh_ctl_ru.fired = "timeout";
  }

  if (_tmesh_ctl_ru.fired != NULL) {
    for (cpu = tme_debug_cpus(); cpu != NULL; cpu = cpu->tme_debug_cpu_next) {
      tme_debug_cpu_stop(cpu, "run_until");
    }
  }
}

/*****************************************************************************/

/* the command handlers.  each fills in result, or returns FALSE with
   err set: */

/* this resumes all CPUs: */
static void
_tmesh_ctl_resume_all(void)
{
  struct tme_debug_cpu *cpu;

  for (cpu = tme_debug_cpus(); cpu != NULL; cpu = cpu->tme_debug_cpu_next) {
    tme_debug_cpu_resume(cpu, 0);
  }
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_hello)
{
  cJSON *cpus, *consoles, *o;
  struct tme_debug_cpu *cpu;
  struct tme_debug_console *console;
  int i;

  cJSON_AddNumberToObject(result, "proto", TMESH_CTL_PROTO_VERSION);
  cJSON_AddStringToObject(result, "version", PACKAGE_VERSION);
  cJSON_AddStringToObject(result, "state", _tmesh_ctl_stopped() ? "stopped" : "running");

  cpus = cJSON_AddArrayToObject(result, "cpus");
  for (cpu = tme_debug_cpus(), i = 0; cpu != NULL; cpu = cpu->tme_debug_cpu_next, i++) {
    o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", i);
    cJSON_AddStringToObject(o, "name", cpu->tme_debug_cpu_name != NULL ? cpu->tme_debug_cpu_name : "");
    cJSON_AddStringToObject(o, "arch", cpu->tme_debug_cpu_arch);
    cJSON_AddStringToObject(o, "model", cpu->tme_debug_cpu_model);
    cJSON_AddNumberToObject(o, "bits", cpu->tme_debug_cpu_bits);
    cJSON_AddStringToObject(o, "endian", cpu->tme_debug_cpu_big_endian ? "big" : "little");
    cJSON_AddItemToArray(cpus, o);
  }

  consoles = cJSON_AddArrayToObject(result, "consoles");
  for (console = tme_debug_consoles(), i = 0; console != NULL; console = console->tme_debug_console_next, i++) {
    o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", i);
    cJSON_AddStringToObject(o, "name", console->tme_debug_console_name != NULL ? console->tme_debug_console_name : "");
    cJSON_AddItemToArray(consoles, o);
  }
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_status)
{
  cJSON *cpus, *o, *stop;
  struct tme_debug_cpu *cpu;
  int i;

  cJSON_AddStringToObject(result, "state", _tmesh_ctl_stopped() ? "stopped" : "running");
  if (_tmesh_ctl_stop_last.reason[0] != '\0') {
    stop = cJSON_AddObjectToObject(result, "stop");
    _tmesh_ctl_add_stop(stop, &_tmesh_ctl_stop_last);
    cJSON_AddNumberToObject(stop, "ninstrs", (double) _tmesh_ctl_stop_last.insns);
  }
  cpus = cJSON_AddArrayToObject(result, "cpus");
  for (cpu = tme_debug_cpus(), i = 0; cpu != NULL; cpu = cpu->tme_debug_cpu_next, i++) {
    o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", i);
    _tmesh_ctl_add_addr(o, "pc", cpu, (*cpu->tme_debug_cpu_pc)(cpu));
    cJSON_AddBoolToObject(o, "stopped", cpu->tme_debug_cpu_stopped);
    cJSON_AddItemToArray(cpus, o);
  }
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_pause)
{
  struct tme_debug_cpu *cpu;
  int stopped;

  if (tme_debug_cpus() == NULL) {
    return (_tmesh_ctl_fail(err, "no_such_cpu", "there are no CPUs"));
  }
  stopped = _tmesh_ctl_stopped();
  cJSON_AddBoolToObject(result, "already_stopped", stopped);
  for (cpu = tme_debug_cpus(); cpu != NULL; cpu = cpu->tme_debug_cpu_next) {
    tme_debug_cpu_stop(cpu, "pause");
  }
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_continue)
{
  cJSON_AddBoolToObject(result, "already_running", !_tmesh_ctl_stopped());
  _tmesh_ctl_resume_all();
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_step)
{
  struct tme_debug_cpu *cpu;
  tme_int64_t n;

  if (!_tmesh_ctl_get_cpu(req, &cpu, err)
      || !_tmesh_ctl_get_int(req, "n", 1, &n, err)) {
    return (FALSE);
  }
  if (n < 1 || n > 0x7fffffff) {
    return (_tmesh_ctl_fail(err, "bad_arg", "'n' should be at least 1"));
  }
  if (!cpu->tme_debug_cpu_stopped) {
    return (_tmesh_ctl_fail(err, "not_stopped", "the machine is running; pause it first"));
  }
  tme_debug_cpu_resume(cpu, n);
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_quit)
{
  /* the response and the exited event are sent, then tmesh exits: */
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_raw)
{
  cJSON *line;
  char *text;
  int rc;

  line = cJSON_GetObjectItemCaseSensitive(req, "line");
  if (!cJSON_IsString(line) || line->valuestring[0] == '\0') {
    return (_tmesh_ctl_fail(err, "bad_arg", "'line' should be a tmesh command"));
  }
  if (strchr(line->valuestring, '\n') != NULL) {
    return (_tmesh_ctl_fail(err, "bad_arg", "'line' should be one line"));
  }
  text = NULL;
  rc = (*_tmesh_ctl_command)(line->valuestring, &text);
  if (rc != TME_OK) {
    free(text);
    return (_tmesh_ctl_fail(err, "bad_arg", "%s", strerror(rc)));
  }
  cJSON_AddStringToObject(result, "text", text != NULL ? text : "");
  free(text);
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_regs)
{
  cJSON *names, *regs, *n;
  struct tme_debug_cpu *cpu;
  struct tme_debug_reg reg;
  unsigned int reg_i;
  int found;
  char buf[24];

  names = cJSON_GetObjectItemCaseSensitive(req, "names");
  if (!_tmesh_ctl_get_cpu(req, &cpu, err)) {
    return (FALSE);
  }
  if (names != NULL && !cJSON_IsArray(names)) {
    return (_tmesh_ctl_fail(err, "bad_arg", "'names' should be a list of register names"));
  }
  cJSON_AddNumberToObject(result, "cpu", _tmesh_ctl_cpu_index(cpu));
  regs = cJSON_AddObjectToObject(result, "regs");

  /* all registers, in their order: */
  if (names == NULL) {
    for (reg_i = 0; (*cpu->tme_debug_cpu_reg)(cpu, reg_i, &reg); reg_i++) {
      snprintf(buf, sizeof(buf), "0x%0*llx", (int) (reg.tme_debug_reg_bits + 3) / 4,
	       (unsigned long long) reg.tme_debug_reg_value);
      cJSON_AddStringToObject(regs, reg.tme_debug_reg_name, buf);
    }
    return (TRUE);
  }

  /* or those asked for: */
  cJSON_ArrayForEach(n, names) {
    if (!cJSON_IsString(n)) {
      return (_tmesh_ctl_fail(err, "bad_arg", "'names' should be a list of register names"));
    }
    found = FALSE;
    for (reg_i = 0; (*cpu->tme_debug_cpu_reg)(cpu, reg_i, &reg); reg_i++) {
      if (!strcmp(reg.tme_debug_reg_name, n->valuestring)) {
	snprintf(buf, sizeof(buf), "0x%0*llx", (int) (reg.tme_debug_reg_bits + 3) / 4,
		 (unsigned long long) reg.tme_debug_reg_value);
	cJSON_AddStringToObject(regs, reg.tme_debug_reg_name, buf);
	found = TRUE;
	break;
      }
    }
    if (!found) {
      return (_tmesh_ctl_fail(err, "bad_arg", "no register '%s'", n->valuestring));
    }
  }
  return (TRUE);
}

static char *_tmesh_ctl_encode_console _TME_P((const tme_uint8_t *, size_t, int));
static tme_uint8_t *_tmesh_ctl_decode_input _TME_P((const char *, int, size_t *, struct _tmesh_ctl_error *));

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_reg_set)
{
  cJSON *name;
  struct tme_debug_cpu *cpu;
  struct tme_debug_reg reg;
  tme_uint64_t value;
  unsigned int reg_i;
  char buf[24];

  name = cJSON_GetObjectItemCaseSensitive(req, "name");
  if (!_tmesh_ctl_get_cpu(req, &cpu, err)
      || !_tmesh_ctl_get_addr(req, "value", cpu, &value, err)) {
    return (FALSE);
  }
  if (!cJSON_IsString(name)) {
    return (_tmesh_ctl_fail(err, "bad_arg", "'name' should be a register name"));
  }
  if (!cpu->tme_debug_cpu_stopped) {
    return (_tmesh_ctl_fail(err, "not_stopped", "the machine is running; pause it first"));
  }
  if (cpu->tme_debug_cpu_reg_set == NULL
      || !(*cpu->tme_debug_cpu_reg_set)(cpu, name->valuestring, value)) {
    return (_tmesh_ctl_fail(err, "bad_arg", "no register '%s', or it can't be written", name->valuestring));
  }

  /* the value as now stored: */
  for (reg_i = 0; (*cpu->tme_debug_cpu_reg)(cpu, reg_i, &reg); reg_i++) {
    if (!strcmp(reg.tme_debug_reg_name, name->valuestring)) {
      snprintf(buf, sizeof(buf), "0x%0*llx", (int) (reg.tme_debug_reg_bits + 3) / 4,
	       (unsigned long long) reg.tme_debug_reg_value);
      cJSON_AddStringToObject(result, "value", buf);
      break;
    }
  }
  return (TRUE);
}

/* the "space" parameter: absent or "virtual" for the CPU's usual data
   space, or a number: */
static int
_tmesh_ctl_get_space(cJSON *req, int *_space, struct _tmesh_ctl_error *err)
{
  cJSON *item;
  tme_int64_t space;

  item = cJSON_GetObjectItemCaseSensitive(req, "space");
  *_space = -1;
  if (item == NULL || cJSON_IsNull(item)
      || (cJSON_IsString(item) && !strcmp(item->valuestring, "virtual"))) {
    return (TRUE);
  }
  if (!_tmesh_ctl_get_int(req, "space", -1, &space, err) || space < 0 || space > 255) {
    return (_tmesh_ctl_fail(err, "bad_arg", "'space' should be \"virtual\", or a number (an m68k function code, or a SPARC ASI)"));
  }
  *_space = space;
  return (TRUE);
}

static void
_tmesh_ctl_add_hex(cJSON *o, const char *key, const tme_uint8_t *buf, size_t count)
{
  char *hex;

  hex = _tmesh_ctl_encode_console(buf, count, TRUE);
  cJSON_AddStringToObject(o, key, hex);
  tme_free(hex);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_mem_read)
{
  struct tme_debug_cpu *cpu;
  tme_uint64_t addr;
  tme_int64_t len;
  tme_uint8_t *buf;
  unsigned int count;
  int space;

  if (!_tmesh_ctl_get_cpu(req, &cpu, err)
      || !_tmesh_ctl_get_addr(req, "addr", cpu, &addr, err)
      || !_tmesh_ctl_get_int(req, "len", 64, &len, err)
      || !_tmesh_ctl_get_space(req, &space, err)) {
    return (FALSE);
  }
  if (len < 1 || len > TMESH_CTL_READ_MAX) {
    return (_tmesh_ctl_fail(err, "bad_arg", "'len' should be 1 to %d", TMESH_CTL_READ_MAX));
  }
  if (cpu->tme_debug_cpu_mem == NULL) {
    return (_tmesh_ctl_fail(err, "bad_arg", "this CPU's memory can't be read"));
  }
  buf = tme_new(tme_uint8_t, len);
  count = (*cpu->tme_debug_cpu_mem)(cpu, addr, buf, len, FALSE, space);
  if (count == 0) {
    tme_free(buf);
    return (_tmesh_ctl_fail(err, "mem_fault", "can't read memory at 0x%llx", (unsigned long long) addr));
  }
  _tmesh_ctl_add_addr(result, "addr", cpu, addr);
  _tmesh_ctl_add_hex(result, "data", buf, count);
  if (count < len) {
    _tmesh_ctl_add_addr(result, "fault_at", cpu, addr + count);
  }
  tme_free(buf);
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_mem_write)
{
  struct tme_debug_cpu *cpu;
  cJSON *data;
  tme_uint64_t addr;
  tme_uint8_t *bytes;
  size_t count;
  unsigned int written;
  int space;

  data = cJSON_GetObjectItemCaseSensitive(req, "data");
  if (!_tmesh_ctl_get_cpu(req, &cpu, err)
      || !_tmesh_ctl_get_addr(req, "addr", cpu, &addr, err)
      || !_tmesh_ctl_get_space(req, &space, err)) {
    return (FALSE);
  }
  if (!cJSON_IsString(data) || data->valuestring[0] == '\0') {
    return (_tmesh_ctl_fail(err, "bad_arg", "'data' should be hex digits, two per byte"));
  }
  if (!cpu->tme_debug_cpu_stopped) {
    return (_tmesh_ctl_fail(err, "not_stopped", "the machine is running; pause it first"));
  }
  if (cpu->tme_debug_cpu_mem == NULL) {
    return (_tmesh_ctl_fail(err, "bad_arg", "this CPU's memory can't be written"));
  }
  bytes = _tmesh_ctl_decode_input(data->valuestring, TRUE, &count, err);
  if (bytes == NULL) {
    return (FALSE);
  }
  written = (*cpu->tme_debug_cpu_mem)(cpu, addr, bytes, count, TRUE, space);
  tme_free(bytes);
  if (written == 0) {
    return (_tmesh_ctl_fail(err, "mem_fault", "can't write memory at 0x%llx", (unsigned long long) addr));
  }
  cJSON_AddNumberToObject(result, "written", written);
  if (written < count) {
    _tmesh_ctl_add_addr(result, "fault_at", cpu, addr + written);
  }
  return (TRUE);
}

static void
_tmesh_ctl_add_breakpoint(cJSON *o, struct tme_debug_cpu *cpu, unsigned int bp_i)
{
  cJSON_AddNumberToObject(o, "id", cpu->tme_debug_cpu_breakpoint_id[bp_i]);
  cJSON_AddNumberToObject(o, "cpu", _tmesh_ctl_cpu_index(cpu));
  _tmesh_ctl_add_addr(o, "addr", cpu, cpu->tme_debug_cpu_breakpoint_addr[bp_i]);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_break_add)
{
  struct tme_debug_cpu *cpu;
  tme_uint64_t addr;
  int id;

  if (!_tmesh_ctl_get_cpu(req, &cpu, err)
      || !_tmesh_ctl_get_addr(req, "pc", cpu, &addr, err)) {
    return (FALSE);
  }
  id = ++_tmesh_ctl_bp_next;
  tme_debug_cpu_breakpoint_add(cpu, addr, id);
  cJSON_AddNumberToObject(result, "id", id);
  cJSON_AddNumberToObject(result, "cpu", _tmesh_ctl_cpu_index(cpu));
  _tmesh_ctl_add_addr(result, "addr", cpu, addr);
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_break_del)
{
  struct tme_debug_cpu *cpu;
  tme_int64_t id;

  if (!_tmesh_ctl_get_int(req, "id", -1, &id, err)) {
    return (FALSE);
  }
  for (cpu = tme_debug_cpus(); cpu != NULL; cpu = cpu->tme_debug_cpu_next) {
    if (id != _tmesh_ctl_ru.bp_id
	&& tme_debug_cpu_breakpoint_remove(cpu, id) == TME_OK) {
      return (TRUE);
    }
  }
  return (_tmesh_ctl_fail(err, "no_such_breakpoint", "no breakpoint with id %lld", (long long) id));
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_break_list)
{
  cJSON *list, *o;
  struct tme_debug_cpu *cpu;
  unsigned int bp_i;

  list = cJSON_AddArrayToObject(result, "breakpoints");
  for (cpu = tme_debug_cpus(); cpu != NULL; cpu = cpu->tme_debug_cpu_next) {
    for (bp_i = 0; bp_i < cpu->tme_debug_cpu_breakpoint_count; bp_i++) {
      if (cpu->tme_debug_cpu_breakpoint_id[bp_i] == _tmesh_ctl_ru.bp_id) {
	continue;
      }
      o = cJSON_CreateObject();
      _tmesh_ctl_add_breakpoint(o, cpu, bp_i);
      cJSON_AddItemToArray(list, o);
    }
  }
  return (TRUE);
}

static int
_tmesh_ctl_get_hex_encoding(cJSON *req, int *_hex, struct _tmesh_ctl_error *err)
{
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(req, "encoding");
  *_hex = FALSE;
  if (item == NULL || cJSON_IsNull(item)
      || (cJSON_IsString(item) && !strcmp(item->valuestring, "text"))) {
    return (TRUE);
  }
  if (cJSON_IsString(item) && !strcmp(item->valuestring, "hex")) {
    *_hex = TRUE;
    return (TRUE);
  }
  return (_tmesh_ctl_fail(err, "bad_arg", "'encoding' should be \"text\" or \"hex\""));
}

/* console output for JSON (malloced).  text: bytes 0x80-0xff as
   U+0080-U+00FF, in UTF-8, without NULs.  hex: two digits per byte: */
static char *
_tmesh_ctl_encode_console(const tme_uint8_t *buf, size_t count, int hex)
{
  char *out, *p;
  size_t i;
  tme_uint8_t b;

  out = tme_new(char, 2 * count + 1);
  p = out;
  for (i = 0; i < count; i++) {
    b = buf[i];
    if (hex) {
      snprintf(p, 3, "%02x", b);
      p += 2;
    }
    else if (b >= 0x80) {
      *(p++) = 0xc0 | (b >> 6);
      *(p++) = 0x80 | (b & 0x3f);
    }
    else if (b != 0) {
      *(p++) = b;
    }
  }
  *p = '\0';
  return (out);
}

static int
_tmesh_ctl_hexdigit(int c)
{
  if (c >= '0' && c <= '9') return (c - '0');
  if (c >= 'a' && c <= 'f') return (c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (c - 'A' + 10);
  return (-1);
}

/* this decodes console input from JSON (malloced): */
static tme_uint8_t *
_tmesh_ctl_decode_input(const char *s, int hex, size_t *_count, struct _tmesh_ctl_error *err)
{
  tme_uint8_t *data;
  size_t len, i;

  len = strlen(s);
  if (hex) {
    if (len % 2 != 0) {
      _tmesh_ctl_fail(err, "bad_arg", "odd number of hex digits");
      return (NULL);
    }
    for (i = 0; i < len; i++) {
      if (_tmesh_ctl_hexdigit(s[i]) < 0) {
	_tmesh_ctl_fail(err, "bad_arg", "not hex: '%c'", s[i]);
	return (NULL);
      }
    }
    len /= 2;
  }
  data = tme_new(tme_uint8_t, len + 1);
  for (i = 0; i < len; i++) {
    data[i] = (hex
	       ? _tmesh_ctl_hexdigit(s[2 * i]) * 16 + _tmesh_ctl_hexdigit(s[2 * i + 1])
	       : (tme_uint8_t) s[i]);
  }
  *_count = len;
  return (data);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_console_read)
{
  struct tme_debug_console *console;
  tme_int64_t since, max;
  tme_uint64_t start, total;
  tme_uint8_t *buf;
  char *out;
  unsigned int count;
  int hex;

  if (!_tmesh_ctl_get_console(req, &console, err)
      || !_tmesh_ctl_get_int(req, "since", 0, &since, err)
      || !_tmesh_ctl_get_int(req, "max", TMESH_CTL_READ_DEFAULT, &max, err)
      || !_tmesh_ctl_get_hex_encoding(req, &hex, err)) {
    return (FALSE);
  }
  if (since < 0) {
    since = 0;
  }
  if (max < 1 || max > TMESH_CTL_READ_MAX) {
    max = TMESH_CTL_READ_MAX;
  }
  buf = tme_new(tme_uint8_t, max);
  count = tme_debug_console_read(console, since, buf, max, &start, &total);
  out = _tmesh_ctl_encode_console(buf, count, hex);
  cJSON_AddStringToObject(result, "data", out);
  cJSON_AddNumberToObject(result, "start", (double) start);
  cJSON_AddNumberToObject(result, "offset", (double) (start + count));
  cJSON_AddBoolToObject(result, "dropped", start > (tme_uint64_t) since);
  tme_free(out);
  tme_free(buf);
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_console_write)
{
  struct tme_debug_console *console;
  cJSON *data;
  tme_uint8_t *bytes;
  size_t count;
  int hex;

  data = cJSON_GetObjectItemCaseSensitive(req, "data");
  if (!_tmesh_ctl_get_console(req, &console, err)
      || !_tmesh_ctl_get_hex_encoding(req, &hex, err)) {
    return (FALSE);
  }
  if (!cJSON_IsString(data)) {
    return (_tmesh_ctl_fail(err, "bad_arg", "'data' should be a string"));
  }
  bytes = _tmesh_ctl_decode_input(data->valuestring, hex, &count, err);
  if (bytes == NULL) {
    return (FALSE);
  }
  cJSON_AddNumberToObject(result, "written",
			  (*console->tme_debug_console_input)(console, bytes, count));
  tme_free(bytes);
  return (TRUE);
}

_TMESH_CTL_HANDLER(_tmesh_ctl_cmd_run_until)
{
  cJSON *console_req, *pc, *pattern, *p, *input;
  struct tme_debug_console *console;
  struct tme_debug_cpu *cpu;
  tme_int64_t insns, timeout_ms, since;
  tme_uint64_t addr, start, total;
  tme_uint8_t *bytes;
  size_t count;
  int n, i;

  console_req = cJSON_GetObjectItemCaseSensitive(req, "console");
  pc = cJSON_GetObjectItemCaseSensitive(req, "pc");
  console = NULL;
  cpu = NULL;
  addr = 0;
  n = 0;
  if (tme_debug_cpus() == NULL) {
    return (_tmesh_ctl_fail(err, "no_such_cpu", "there are no CPUs"));
  }
  if (!_tmesh_ctl_get_int(req, "insns", -1, &insns, err)
      || !_tmesh_ctl_get_int(req, "timeout_ms", -1, &timeout_ms, err)) {
    return (FALSE);
  }
  if (console_req == NULL && pc == NULL && insns < 0 && timeout_ms < 0) {
    return (_tmesh_ctl_fail(err, "bad_arg", "give at least one of 'console', 'pc', 'insns', 'timeout_ms'"));
  }

  if (console_req != NULL) {
    pattern = cJSON_GetObjectItemCaseSensitive(console_req, "pattern");
    if (!cJSON_IsObject(console_req)
	|| !(cJSON_IsString(pattern) || cJSON_IsArray(pattern))) {
      return (_tmesh_ctl_fail(err, "bad_arg", "'console' should be {\"pattern\": string or list of strings}"));
    }
    if (!_tmesh_ctl_get_console(console_req, &console, err)) {
      return (FALSE);
    }
    n = (cJSON_IsArray(pattern) ? cJSON_GetArraySize(pattern) : 1);
    if (n == 0) {
      return (_tmesh_ctl_fail(err, "bad_arg", "no patterns"));
    }
    for (i = 0; i < n; i++) {
      p = (cJSON_IsArray(pattern) ? cJSON_GetArrayItem(pattern, i) : pattern);
      if (!cJSON_IsString(p) || p->valuestring[0] == '\0') {
	return (_tmesh_ctl_fail(err, "bad_arg", "patterns should be non-empty strings"));
      }
    }
    input = cJSON_GetObjectItemCaseSensitive(console_req, "input");
    if (input != NULL && !cJSON_IsString(input)) {
      return (_tmesh_ctl_fail(err, "bad_arg", "'input' should be a string"));
    }
  }
  if (pc != NULL) {
    if (!_tmesh_ctl_get_cpu(req, &cpu, err)
	|| !_tmesh_ctl_get_addr(req, "pc", cpu, &addr, err)) {
      return (FALSE);
    }
  }

  /* a new run_until replaces any earlier one: */
  _tmesh_ctl_run_until_clear();
  _tmesh_ctl_ru.active = TRUE;

  if (console_req != NULL) {
    pattern = cJSON_GetObjectItemCaseSensitive(console_req, "pattern");
    _tmesh_ctl_ru.console = console;
    _tmesh_ctl_ru.patterns = tme_new(char *, n);
    for (i = 0; i < n; i++) {
      p = (cJSON_IsArray(pattern) ? cJSON_GetArrayItem(pattern, i) : pattern);
      _tmesh_ctl_ru.patterns[i] = strdup(p->valuestring);
      if (strlen(p->valuestring) > _tmesh_ctl_ru.maxlen) {
	_tmesh_ctl_ru.maxlen = strlen(p->valuestring);
      }
    }
    _tmesh_ctl_ru.n_patterns = n;

    /* from now on, or from "since".  (the first search starts
       maxlen - 1 before checked.) */
    (void) tme_debug_console_read(console, ~(tme_uint64_t) 0, NULL, 0, &start, &total);
    if (!_tmesh_ctl_get_int(console_req, "since", total, &since, err)) {
      _tmesh_ctl_run_until_clear();
      return (FALSE);
    }
    if (since < 0 || (tme_uint64_t) since > total) {
      since = total;
    }
    _tmesh_ctl_ru.checked = since + _tmesh_ctl_ru.maxlen - 1;
    cJSON_AddNumberToObject(result, "console_offset", (double) since);

    /* type the input, now that its reply will be seen: */
    input = cJSON_GetObjectItemCaseSensitive(console_req, "input");
    if (input != NULL) {
      bytes = _tmesh_ctl_decode_input(input->valuestring, FALSE, &count, err);
      cJSON_AddNumberToObject(result, "written",
			      (*console->tme_debug_console_input)(console, bytes, count));
      tme_free(bytes);
    }
  }

  if (pc != NULL) {
    _tmesh_ctl_ru.bp_cpu = cpu;
    _tmesh_ctl_ru.bp_id = ++_tmesh_ctl_bp_next;
    tme_debug_cpu_breakpoint_add(cpu, addr, _tmesh_ctl_ru.bp_id);
  }
  if (insns >= 0) {
    _tmesh_ctl_ru.has_insns = TRUE;
    _tmesh_ctl_ru.insns_end = _tmesh_ctl_insns() + insns;
  }
  if (timeout_ms >= 0) {
    _tmesh_ctl_ru.has_timeout = TRUE;
    _tmesh_ctl_ru.deadline_usec = _tmesh_ctl_now() + timeout_ms * 1000;
  }

  cJSON_AddBoolToObject(result, "already_running", !_tmesh_ctl_stopped());
  _tmesh_ctl_resume_all();
  return (TRUE);
}

static const struct {
  const char *name;
  int (*func) _TME_P((cJSON *, cJSON *, struct _tmesh_ctl_error *));
} _tmesh_ctl_cmds[] = {
  { "hello", _tmesh_ctl_cmd_hello },
  { "status", _tmesh_ctl_cmd_status },
  { "pause", _tmesh_ctl_cmd_pause },
  { "continue", _tmesh_ctl_cmd_continue },
  { "step", _tmesh_ctl_cmd_step },
  { "quit", _tmesh_ctl_cmd_quit },
  { "raw", _tmesh_ctl_cmd_raw },
  { "regs", _tmesh_ctl_cmd_regs },
  { "reg_set", _tmesh_ctl_cmd_reg_set },
  { "mem_read", _tmesh_ctl_cmd_mem_read },
  { "mem_write", _tmesh_ctl_cmd_mem_write },
  { "break_add", _tmesh_ctl_cmd_break_add },
  { "break_del", _tmesh_ctl_cmd_break_del },
  { "break_list", _tmesh_ctl_cmd_break_list },
  { "console_read", _tmesh_ctl_cmd_console_read },
  { "console_write", _tmesh_ctl_cmd_console_write },
  { "run_until", _tmesh_ctl_cmd_run_until },
  { NULL, NULL }
};

/* this stops listening, and removes a Unix socket: */
static void
_tmesh_ctl_close(void)
{
  if (_tmesh_ctl_listen_fd >= 0) {
    close(_tmesh_ctl_listen_fd);
    _tmesh_ctl_listen_fd = -1;
  }
  if (_tmesh_ctl_unix_path != NULL) {
    unlink(_tmesh_ctl_unix_path);
    free(_tmesh_ctl_unix_path);
    _tmesh_ctl_unix_path = NULL;
  }
}

/* this handles one request, and sends its response: */
static void
_tmesh_ctl_handle_line(const char *line, size_t len)
{
  cJSON *req, *resp, *result, *cmd, *id, *e, *ev;
  struct _tmesh_ctl_error err;
  int ok, quit, k;
  size_t i;

  /* ignore empty lines: */
  for (i = 0; i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'); i++);
  if (i == len) {
    return;
  }

  err.code = NULL;
  err.message[0] = '\0';
  ok = FALSE;
  quit = FALSE;
  result = NULL;
  resp = cJSON_CreateObject();
  req = cJSON_ParseWithLength(line, len);

  id = cJSON_GetObjectItemCaseSensitive(req, "id");
  if (id != NULL) {
    cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, TRUE));
  }

  cmd = cJSON_GetObjectItemCaseSensitive(req, "cmd");
  if (!cJSON_IsObject(req)) {
    _tmesh_ctl_fail(&err, "bad_request", "not a JSON object");
  }
  else if (!cJSON_IsString(cmd)) {
    _tmesh_ctl_fail(&err, "bad_request", "no \"cmd\"");
  }
  else {
    for (k = 0; _tmesh_ctl_cmds[k].name != NULL; k++) {
      if (!strcmp(_tmesh_ctl_cmds[k].name, cmd->valuestring)) {
	break;
      }
    }
    if (_tmesh_ctl_cmds[k].name == NULL) {
      _tmesh_ctl_fail(&err, "unknown_cmd", "unknown command '%s'", cmd->valuestring);
    }
    else {
      result = cJSON_CreateObject();
      ok = (*_tmesh_ctl_cmds[k].func)(req, result, &err);
      quit = (ok && _tmesh_ctl_cmds[k].func == _tmesh_ctl_cmd_quit);
    }
  }

  cJSON_AddBoolToObject(resp, "ok", ok);
  if (ok) {
    cJSON_AddItemToObject(resp, "result", result);
  }
  else {
    e = cJSON_AddObjectToObject(resp, "error");
    cJSON_AddStringToObject(e, "code", err.code);
    cJSON_AddStringToObject(e, "message", err.message);
    cJSON_Delete(result);
  }
  cJSON_AddNumberToObject(resp, "ninstrs", (double) _tmesh_ctl_insns());
  cJSON_Delete(req);
  _tmesh_ctl_send(resp);

  /* quit: */
  if (quit) {
    ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "event", "exited");
    cJSON_AddNumberToObject(ev, "ninstrs", (double) _tmesh_ctl_insns());
    _tmesh_ctl_send(ev);
    _tmesh_ctl_disconnect();
    _tmesh_ctl_close();
    exit(0);
  }
}

static void
_tmesh_ctl_accept(void)
{
  const char *busy;
  int fd, flags;
#ifdef SO_NOSIGPIPE
  int one;
#endif
  struct timeval tv;

  while ((fd = accept(_tmesh_ctl_listen_fd, NULL, NULL)) >= 0) {
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (void *) &one, sizeof(one));
#endif

    /* one client at a time: */
    if (_tmesh_ctl_client_fd >= 0) {
      busy = "{\"ok\":false,\"error\":{\"code\":\"busy\",\"message\":\"another client is connected\"}}\n";
      (void) _tmesh_ctl_send_all(fd, busy, strlen(busy));
      close(fd);
      continue;
    }

    /* reads don't block, and a client that doesn't read its
       responses for a while is dropped: */
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    tv.tv_sec = TMESH_CTL_SEND_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (void *) &tv, sizeof(tv));
    _tmesh_ctl_client_fd = fd;
    _tmesh_ctl_inlen = 0;
  }
}

static void
_tmesh_ctl_read(void)
{
  char buf[4096];
  char *nl;
  ssize_t rc;
  size_t linelen;
  cJSON *resp, *e;

  while (_tmesh_ctl_client_fd >= 0) {
    rc = recv(_tmesh_ctl_client_fd, buf, sizeof(buf), 0);
    if (rc <= 0) {
      if (rc == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
	_tmesh_ctl_disconnect();
      }
      return;
    }
    if (_tmesh_ctl_inlen + rc > TMESH_CTL_MAX_LINE) {
      resp = cJSON_CreateObject();
      cJSON_AddBoolToObject(resp, "ok", FALSE);
      e = cJSON_AddObjectToObject(resp, "error");
      cJSON_AddStringToObject(e, "code", "bad_request");
      cJSON_AddStringToObject(e, "message", "line too long");
      _tmesh_ctl_send(resp);
      _tmesh_ctl_disconnect();
      return;
    }
    memcpy(_tmesh_ctl_inbuf + _tmesh_ctl_inlen, buf, rc);
    _tmesh_ctl_inlen += rc;

    /* handle each complete line: */
    while (_tmesh_ctl_client_fd >= 0
	   && (nl = memchr(_tmesh_ctl_inbuf, '\n', _tmesh_ctl_inlen)) != NULL) {
      linelen = nl - _tmesh_ctl_inbuf;
      _tmesh_ctl_handle_line(_tmesh_ctl_inbuf, linelen);
      if (_tmesh_ctl_client_fd < 0) {
	return;
      }
      memmove(_tmesh_ctl_inbuf, nl + 1, _tmesh_ctl_inlen - linelen - 1);
      _tmesh_ctl_inlen -= linelen + 1;
    }
  }
}

/* the control socket thread: */
static _tme_thret
_tmesh_ctl_th(void *unused)
{
  struct event_set_return esr[2];
  struct timeval tv;
  tme_event_set_t *events;
  int maxevents;

  tme_thread_enter(NULL);

  /* NB that with cooperative threads, this thread starts over from
     the beginning when the wait below yields: */
  for (;;) {

    /* handle any stop, any run_until condition, and any requests.
       reading first notices a client which has gone, before a new
       one is turned away as busy: */
    _tmesh_ctl_stop_handle();
    _tmesh_ctl_run_until_check();
    _tmesh_ctl_read();
    _tmesh_ctl_accept();
    _tmesh_ctl_read();
    _tmesh_ctl_stop_handle();

    /* wait for a request, or a few milliseconds.  with cooperative
       threads, an event set that is waited on belongs to the threads
       system, which frees it, unless the wait returns: */
    maxevents = 2;
    events = tme_event_set_init(&maxevents, EVENT_METHOD_FAST);
    tme_event_reset(events);
    tme_event_ctl(events, _tmesh_ctl_listen_fd, EVENT_READ, 0);
    if (_tmesh_ctl_client_fd >= 0) {
      tme_event_ctl(events, _tmesh_ctl_client_fd, EVENT_READ, 0);
    }
    tv.tv_sec = 0;
    tv.tv_usec = TMESH_CTL_POLL_USEC;
    (void) tme_event_wait(events, &tv, esr, 2, NULL);
    tme_event_free(events);
  }
  /* NOTREACHED */
  tme_thread_exit(NULL);
}

/* this starts listening, as given by --control: "unix:PATH" or
   "tcp:PORT".  it exits on errors: */
void
tmesh_ctl_start(const char *spec, int (*command) _TME_P((const char *, char **)))
{
  struct sockaddr_in sin;
  struct sockaddr_un sun;
  socklen_t sinlen;
  struct stat st;
  const char *path;
  char *end;
  long port;
  int one, flags;
  tme_threadid_t thread;

  _tmesh_ctl_command = command;
  one = 1;

  if (!strncmp(spec, "tcp:", 4)) {
    port = strtol(spec + 4, &end, 10);
    if (spec[4] == '\0' || *end != '\0' || port < 0 || port > 65535) {
      fprintf(stderr, "%s: --control: bad port in '%s'\n", argv0, spec);
      exit(1);
    }
    _tmesh_ctl_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_tmesh_ctl_listen_fd < 0) {
      perror("--control: socket");
      exit(1);
    }
    setsockopt(_tmesh_ctl_listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *) &one, sizeof(one));
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons(port);
    if (bind(_tmesh_ctl_listen_fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
      fprintf(stderr, "%s: --control: can't listen on port %ld: %s\n", argv0, port, strerror(errno));
      exit(1);
    }
    sinlen = sizeof(sin);
    getsockname(_tmesh_ctl_listen_fd, (struct sockaddr *) &sin, &sinlen);
    fprintf(stderr, "%s: control socket: tcp:%d\n", argv0, ntohs(sin.sin_port));
  }
  else if (!strncmp(spec, "unix:", 5)) {
    path = spec + 5;
    if (path[0] == '\0' || strlen(path) >= sizeof(sun.sun_path)) {
      fprintf(stderr, "%s: --control: bad path in '%s'\n", argv0, spec);
      exit(1);
    }

    /* replace an old socket, but nothing else: */
    if (lstat(path, &st) == 0) {
      if (!S_ISSOCK(st.st_mode)) {
	fprintf(stderr, "%s: --control: %s exists, and isn't a socket\n", argv0, path);
	exit(1);
      }
      unlink(path);
    }
    _tmesh_ctl_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_tmesh_ctl_listen_fd < 0) {
      perror("--control: socket");
      exit(1);
    }
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", path);
    if (bind(_tmesh_ctl_listen_fd, (struct sockaddr *) &sun, sizeof(sun)) < 0) {
      fprintf(stderr, "%s: --control: can't listen on %s: %s\n", argv0, path, strerror(errno));
      exit(1);
    }
    _tmesh_ctl_unix_path = strdup(path);
    atexit(_tmesh_ctl_close);
  }
  else {
    fprintf(stderr, "%s: --control: expected unix:PATH or tcp:PORT, not '%s'\n", argv0, spec);
    exit(1);
  }

  if (listen(_tmesh_ctl_listen_fd, 1) < 0) {
    perror("--control: listen");
    exit(1);
  }
  fcntl(_tmesh_ctl_listen_fd, F_SETFD, FD_CLOEXEC);
  flags = fcntl(_tmesh_ctl_listen_fd, F_GETFL);
  fcntl(_tmesh_ctl_listen_fd, F_SETFL, flags | O_NONBLOCK);

  _tmesh_ctl_inbuf = tme_new(char, TMESH_CTL_MAX_LINE);
  tme_debug_set_stop_hook(_tmesh_ctl_stop_hook);
  tme_thread_create(&thread, (tme_thread_t) _tmesh_ctl_th, NULL);
}

/*****************************************************************************/

/* the client: tmesh --ctl SOCKET [--wait] [--timeout SECONDS] COMMAND [ARGUMENT] */

static int
_tmesh_ctl_client_usage(void)
{
  fprintf(stderr,
	  "usage: %s --ctl unix:PATH|tcp:PORT [--wait] [--timeout SECONDS] COMMAND [ARGUMENT]\n"
	  "The argument is a JSON object with the request's parameters, or a plain value\n"
	  "for raw (line), step (n), break_add (pc), break_del (id), console_read (since),\n"
	  "console_write (data), or run_until (a console pattern).\n",
	  argv0);
  return (2);
}

static int
_tmesh_ctl_client_connect(const char *spec)
{
  struct sockaddr_in sin;
  struct sockaddr_un sun;
  int fd;

  if (!strncmp(spec, "tcp:", 4)) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      return (-1);
    }
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons(atoi(spec + 4));
    if (connect(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
      close(fd);
      return (-1);
    }
    return (fd);
  }
  if (!strncmp(spec, "unix:", 5)) {
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      return (-1);
    }
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", spec + 5);
    if (connect(fd, (struct sockaddr *) &sun, sizeof(sun)) < 0) {
      close(fd);
      return (-1);
    }
    return (fd);
  }
  return (-1);
}

/* this reads one line into a malloced string, returning NULL on EOF or
   error, and setting *_timed_out if the deadline (if any) passed
   first: */
static char *
_tmesh_ctl_client_readline(int fd, char **_buf, size_t *_len, const struct timeval *deadline, int *_timed_out)
{
  char tmp[4096];
  char *nl, *line;
  size_t linelen;
  ssize_t rc;
  struct timeval now, tv;
  tme_int64_t left;
  fd_set rfds;

  *_timed_out = FALSE;
  for (;;) {
    nl = (*_len > 0 ? memchr(*_buf, '\n', *_len) : NULL);
    if (nl != NULL) {
      linelen = nl - *_buf;
      line = malloc(linelen + 1);
      memcpy(line, *_buf, linelen);
      line[linelen] = '\0';
      memmove(*_buf, nl + 1, *_len - linelen - 1);
      *_len -= linelen + 1;
      return (line);
    }
    if (deadline != NULL) {
      gettimeofday(&now, NULL);
      left = ((tme_int64_t) (deadline->tv_sec - now.tv_sec) * 1000000
	      + (deadline->tv_usec - now.tv_usec));
      if (left <= 0) {
	*_timed_out = TRUE;
	return (NULL);
      }
      tv.tv_sec = left / 1000000;
      tv.tv_usec = left % 1000000;
      FD_ZERO(&rfds);
      FD_SET(fd, &rfds);
      if (select(fd + 1, &rfds, NULL, NULL, &tv) == 0) {
	*_timed_out = TRUE;
	return (NULL);
      }
    }
    rc = recv(fd, tmp, sizeof(tmp), 0);
    if (rc < 0 && errno == EINTR) {
      continue;
    }
    if (rc <= 0) {
      return (NULL);
    }
    *_buf = realloc(*_buf, *_len + rc + 1);
    memcpy(*_buf + *_len, tmp, rc);
    *_len += rc;
  }
}

int
tmesh_ctl_client(int argc, char **argv)
{
  /* the commands which take a plain value, and its parameter: */
  static const struct {
    const char *cmd, *key;
    int number;
  } plain[] = {
    { "raw", "line", FALSE },
    { "step", "n", TRUE },
    { "break_add", "pc", FALSE },
    { "break_del", "id", TRUE },
    { "console_read", "since", TRUE },
    { "console_write", "data", FALSE },
    { "mem_read", "addr", FALSE },
    { NULL, NULL, FALSE }
  };
  const char *spec, *cmd, *arg;
  struct timeval deadline;
  int wait, have_deadline, timed_out, got_response, wait_for_stop, done;
  char *buf, *line, *reqstr, *end;
  size_t buflen, reqlen;
  cJSON *req, *msg, *ok, *result, *ev, *console;
  double t;
  long long v;
  int arg_i, fd, ret, k;

  arg_i = 2;
  wait = FALSE;
  have_deadline = FALSE;
  arg = NULL;
  if (argc < 4) {
    return (_tmesh_ctl_client_usage());
  }
  spec = argv[arg_i++];
  while (arg_i < argc && !strncmp(argv[arg_i], "--", 2)) {
    if (!strcmp(argv[arg_i], "--wait")) {
      wait = TRUE;
      arg_i++;
    }
    else if (!strcmp(argv[arg_i], "--timeout") && arg_i + 1 < argc) {
      t = atof(argv[arg_i + 1]);
      gettimeofday(&deadline, NULL);
      deadline.tv_sec += (time_t) t;
      deadline.tv_usec += (long) ((t - (time_t) t) * 1e6);
      if (deadline.tv_usec >= 1000000) {
	deadline.tv_sec++;
	deadline.tv_usec -= 1000000;
      }
      have_deadline = TRUE;
      arg_i += 2;
    }
    else {
      return (_tmesh_ctl_client_usage());
    }
  }
  if (arg_i >= argc) {
    return (_tmesh_ctl_client_usage());
  }
  cmd = argv[arg_i++];
  if (arg_i < argc) {
    arg = argv[arg_i++];
  }
  if (arg_i < argc) {
    return (_tmesh_ctl_client_usage());
  }

  /* the request: */
  if (arg != NULL && arg[0] == '{') {
    req = cJSON_Parse(arg);
    if (!cJSON_IsObject(req)) {
      fprintf(stderr, "%s --ctl: not a JSON object: %s\n", argv0, arg);
      return (2);
    }
  }
  else if (arg != NULL && !strcmp(cmd, "run_until")) {
    req = cJSON_CreateObject();
    console = cJSON_AddObjectToObject(req, "console");
    cJSON_AddStringToObject(console, "pattern", arg);
  }
  else {
    req = cJSON_CreateObject();
    if (arg != NULL) {
      for (k = 0; plain[k].cmd != NULL && strcmp(plain[k].cmd, cmd); k++);
      if (plain[k].cmd == NULL) {
	fprintf(stderr, "%s --ctl: %s takes its parameters as a JSON object\n", argv0, cmd);
	return (2);
      }
      if (plain[k].number) {
	v = strtoll(arg, &end, 0);
	if (arg[0] == '\0' || *end != '\0') {
	  fprintf(stderr, "%s --ctl: not a number: %s\n", argv0, arg);
	  return (2);
	}
	cJSON_AddNumberToObject(req, plain[k].key, (double) v);
      }
      else {
	cJSON_AddStringToObject(req, plain[k].key, arg);
      }
    }
  }
  cJSON_DeleteItemFromObjectCaseSensitive(req, "cmd");
  cJSON_AddStringToObject(req, "cmd", cmd);
  if (cJSON_GetObjectItemCaseSensitive(req, "id") == NULL) {
    cJSON_AddNumberToObject(req, "id", 1);
  }

  fd = _tmesh_ctl_client_connect(spec);
  if (fd < 0) {
    fprintf(stderr, "%s --ctl: can't connect to %s\n", argv0, spec);
    cJSON_Delete(req);
    return (2);
  }
  reqstr = cJSON_PrintUnformatted(req);
  cJSON_Delete(req);
  reqlen = strlen(reqstr);
  reqstr = realloc(reqstr, reqlen + 2);
  reqstr[reqlen] = '\n';
  reqstr[reqlen + 1] = '\0';
  if (!_tmesh_ctl_send_all(fd, reqstr, reqlen + 1)) {
    fprintf(stderr, "%s --ctl: can't send to %s\n", argv0, spec);
    close(fd);
    return (2);
  }
  free(reqstr);

  /* the response (events before it are skipped): */
  buf = NULL;
  buflen = 0;
  ret = 2;
  got_response = FALSE;
  wait_for_stop = FALSE;
  timed_out = FALSE;
  while ((line = _tmesh_ctl_client_readline(fd, &buf, &buflen, have_deadline ? &deadline : NULL, &timed_out)) != NULL) {
    msg = cJSON_Parse(line);
    ok = cJSON_GetObjectItemCaseSensitive(msg, "ok");
    if (ok == NULL) {
      cJSON_Delete(msg);
      free(line);
      continue;
    }
    printf("%s\n", line);
    ret = (cJSON_IsTrue(ok) ? 0 : 1);
    result = cJSON_GetObjectItemCaseSensitive(msg, "result");
    wait_for_stop = (wait && ret == 0
		     && (!strcmp(cmd, "continue")
			 || !strcmp(cmd, "step")
			 || !strcmp(cmd, "run_until")
			 || (!strcmp(cmd, "pause")
			     && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "already_stopped")))));
    cJSON_Delete(msg);
    free(line);
    got_response = TRUE;
    break;
  }

  /* and with --wait, the stop: */
  if (got_response && wait_for_stop) {
    ret = 2;
    while ((line = _tmesh_ctl_client_readline(fd, &buf, &buflen, have_deadline ? &deadline : NULL, &timed_out)) != NULL) {
      msg = cJSON_Parse(line);
      ev = cJSON_GetObjectItemCaseSensitive(msg, "event");
      done = (cJSON_IsString(ev)
	      && (!strcmp(ev->valuestring, "stopped")
		  || !strcmp(ev->valuestring, "exited")));
      if (done) {
	printf("%s\n", line);
      }
      cJSON_Delete(msg);
      free(line);
      if (done) {
	ret = 0;
	break;
      }
    }
  }

  if (ret == 2 && timed_out) {
    fprintf(stderr, "%s --ctl: timed out\n", argv0);
    ret = 3;
  }
  else if (ret == 2) {
    fprintf(stderr, "%s --ctl: connection closed\n", argv0);
  }
  fflush(stdout);
  free(buf);
  close(fd);
  return (ret);
}

#else  /* WIN32 */

void
tmesh_ctl_start(const char *spec, int (*command) _TME_P((const char *, char **)))
{
  fprintf(stderr, "%s: --control isn't supported on Windows\n", argv0);
  exit(1);
}

int
tmesh_ctl_client(int argc, char **argv)
{
  fprintf(stderr, "%s: --ctl isn't supported on Windows\n", argv0);
  return (2);
}

#endif /* WIN32 */
