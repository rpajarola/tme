/* libtme/debug.c - debugging CPUs and consoles: */

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

#include <tme/common.h>

/* includes: */
#include <tme/debug.h>
#include <tme/threads.h>
#include <tme/generic/bus.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* an element's name: */
struct _tme_debug_element {
  struct _tme_debug_element *_tme_debug_element_next;
  struct tme_element *_tme_debug_element_element;
  char *_tme_debug_element_name;
};

/* a bus: */
struct _tme_debug_bus {
  struct _tme_debug_bus *_tme_debug_bus_next;
  struct tme_element *_tme_debug_bus_element;
  struct tme_bus *_tme_debug_bus_bus;
};

/* a device's cycle handler, which _tme_debug_cycle() calls after it
   has watched the cycle: */
struct _tme_debug_cycle {
  struct _tme_debug_cycle *_tme_debug_cycle_next;
  struct tme_element *_tme_debug_cycle_element;
  tme_bus_cycle_handler _tme_debug_cycle_handler;
  void *_tme_debug_cycle_private;
};

/* the most cycle handlers that are watched: */
#define _TME_DEBUG_CYCLES_MAX	(1024)

/* the CPUs and the consoles, in the order registered: */
static struct tme_debug_cpu *_tme_debug_cpus;
static struct tme_debug_console *_tme_debug_consoles;

/* the named elements, the buses, and the cycle handlers: */
static struct _tme_debug_element *_tme_debug_elements;
static struct _tme_debug_bus *_tme_debug_buses;

/* the screens, in the order registered: */
static struct tme_debug_screen *_tme_debug_screens;
static struct _tme_debug_cycle *_tme_debug_cycles;
static unsigned int _tme_debug_cycles_count;

/* the watches, and how many there are of each kind: */
static struct tme_debug_watch *_tme_debug_watches;
static unsigned int _tme_debug_watches_exception;
static unsigned int _tme_debug_watches_device;

/* the trace, and how many events have been put in it: */
static struct tme_debug_trace_event *_tme_debug_trace;
static tme_uint64_t _tme_debug_trace_total;

/* this protects the cycle handlers and the trace: */
static tme_mutex_t _tme_debug_mutex;
static int _tme_debug_mutex_ready;

/* this is called when a CPU stops: */
static void (*_tme_debug_stop_hook) _TME_P((struct tme_debug_cpu *));

/* this makes the mutex.  it is first called while tmesh makes the
   machine, with only one thread: */
static void
_tme_debug_init(void)
{
  if (!_tme_debug_mutex_ready) {
    tme_mutex_init(&_tme_debug_mutex);
    _tme_debug_mutex_ready = TRUE;
  }
}

/* this names an element, after tmesh has made it: */
void
tme_debug_name(struct tme_element *element, const char *name)
{
  struct tme_debug_cpu *cpu;
  struct tme_debug_console *console;
  struct _tme_debug_element *named;

  _tme_debug_init();
  for (named = _tme_debug_elements; named != NULL; named = named->_tme_debug_element_next) {
    if (named->_tme_debug_element_element == element) {
      break;
    }
  }
  if (named == NULL) {
    named = tme_new0(struct _tme_debug_element, 1);
    named->_tme_debug_element_element = element;
    named->_tme_debug_element_next = _tme_debug_elements;
    _tme_debug_elements = named;
  }
  else {
    tme_free(named->_tme_debug_element_name);
  }
  named->_tme_debug_element_name = tme_strdup(name);

  for (cpu = _tme_debug_cpus; cpu != NULL; cpu = cpu->tme_debug_cpu_next) {
    if (cpu->tme_debug_cpu_element == element) {
      if (cpu->tme_debug_cpu_name != NULL) {
	tme_free(cpu->tme_debug_cpu_name);
      }
      cpu->tme_debug_cpu_name = tme_strdup(name);
    }
  }
  for (console = _tme_debug_consoles; console != NULL; console = console->tme_debug_console_next) {
    if (console->tme_debug_console_element == element) {
      if (console->tme_debug_console_name != NULL) {
	tme_free(console->tme_debug_console_name);
      }
      console->tme_debug_console_name = tme_strdup(name);
    }
  }
}

/* this returns an element's name, or NULL: */
const char *
tme_debug_element_name(struct tme_element *element)
{
  struct _tme_debug_element *named;

  for (named = _tme_debug_elements; named != NULL; named = named->_tme_debug_element_next) {
    if (named->_tme_debug_element_element == element) {
      return (named->_tme_debug_element_name);
    }
  }
  return (NULL);
}

/* this returns the element with a name, or NULL: */
struct tme_element *
tme_debug_element_find(const char *name)
{
  struct _tme_debug_element *named;

  for (named = _tme_debug_elements; named != NULL; named = named->_tme_debug_element_next) {
    if (!strcmp(named->_tme_debug_element_name, name)) {
      return (named->_tme_debug_element_element);
    }
  }
  return (NULL);
}

/* this registers a CPU: */
void
tme_debug_cpu_register(struct tme_debug_cpu *cpu)
{
  struct tme_debug_cpu **_prev;

  for (_prev = &_tme_debug_cpus;
       *_prev != NULL;
       _prev = &(*_prev)->tme_debug_cpu_next);
  cpu->tme_debug_cpu_next = NULL;
  *_prev = cpu;
}

/* this returns the CPUs: */
struct tme_debug_cpu *
tme_debug_cpus(void)
{
  return (_tme_debug_cpus);
}

/* this sets whether a CPU must check every instruction: */
static void
_tme_debug_cpu_update_slow(struct tme_debug_cpu *cpu)
{
  cpu->tme_debug_cpu_slow
    = (cpu->tme_debug_cpu_stop_requested
       || cpu->tme_debug_cpu_stepping
       || cpu->tme_debug_cpu_breakpoint_count > 0);
}

/* a CPU calls this before an instruction at pc, while
   tme_debug_cpu_slow is nonzero.  it returns nonzero if the CPU must
   stop before running the instruction: */
int
tme_debug_cpu_check(struct tme_debug_cpu *cpu, tme_uint64_t pc)
{
  int skip;
  unsigned int bp_i;

  /* a stop that was asked for: */
  if (cpu->tme_debug_cpu_stop_requested) {
    cpu->tme_debug_cpu_stop_requested = FALSE;
    _tme_debug_cpu_update_slow(cpu);
    return (TRUE);
  }

  /* a breakpoint where we resumed is passed over once: */
  skip = FALSE;
  if (cpu->tme_debug_cpu_skip_valid) {
    skip = (pc == cpu->tme_debug_cpu_skip_pc);
    cpu->tme_debug_cpu_skip_valid = FALSE;
  }

  /* the end of a step: */
  if (cpu->tme_debug_cpu_stepping
      && cpu->tme_debug_cpu_steps == 0) {
    cpu->tme_debug_cpu_stepping = FALSE;
    cpu->tme_debug_cpu_stop_reason = "step";
    _tme_debug_cpu_update_slow(cpu);
    return (TRUE);
  }

  /* a breakpoint: */
  if (!skip) {
    for (bp_i = 0; bp_i < cpu->tme_debug_cpu_breakpoint_count; bp_i++) {
      if (cpu->tme_debug_cpu_breakpoint_addr[bp_i] == pc) {
	cpu->tme_debug_cpu_stepping = FALSE;
	cpu->tme_debug_cpu_stop_reason = "breakpoint";
	cpu->tme_debug_cpu_breakpoint_hit = cpu->tme_debug_cpu_breakpoint_id[bp_i];
	_tme_debug_cpu_update_slow(cpu);
	return (TRUE);
      }
    }
  }

  /* this instruction runs: */
  if (cpu->tme_debug_cpu_stepping) {
    cpu->tme_debug_cpu_steps--;
  }
  return (FALSE);
}

/* a CPU calls this once when it has stopped: */
void
tme_debug_cpu_stopped(struct tme_debug_cpu *cpu)
{
  cpu->tme_debug_cpu_stop_requested = FALSE;
  cpu->tme_debug_cpu_stopped = TRUE;
  _tme_debug_cpu_update_slow(cpu);
  if (_tme_debug_stop_hook != NULL) {
    (*_tme_debug_stop_hook)(cpu);
  }
  cpu->tme_debug_cpu_breakpoint_hit = 0;
  cpu->tme_debug_cpu_watch_hit = 0;
}

/* a stopped CPU calls this when it is woken up.  it returns nonzero if
   the CPU must resume: */
int
tme_debug_cpu_resuming(struct tme_debug_cpu *cpu)
{
  if (!cpu->tme_debug_cpu_resume_requested) {
    return (FALSE);
  }
  cpu->tme_debug_cpu_resume_requested = FALSE;
  return (TRUE);
}

/* the debugger calls this to stop a CPU, for the reason given: */
void
tme_debug_cpu_stop(struct tme_debug_cpu *cpu, const char *reason)
{
  if (cpu->tme_debug_cpu_stopped
      || cpu->tme_debug_cpu_stop_requested) {
    return;
  }
  cpu->tme_debug_cpu_stop_reason = reason;
  cpu->tme_debug_cpu_stop_requested = TRUE;
  _tme_debug_cpu_update_slow(cpu);
  (*cpu->tme_debug_cpu_wakeup)(cpu);
}

/* the debugger calls this to resume a stopped CPU, for steps
   instructions, or until something stops it if steps is zero: */
void
tme_debug_cpu_resume(struct tme_debug_cpu *cpu, unsigned long steps)
{
  /* a CPU that hasn't stopped yet doesn't have to: */
  if (!cpu->tme_debug_cpu_stopped) {
    cpu->tme_debug_cpu_stop_requested = FALSE;
    _tme_debug_cpu_update_slow(cpu);
    return;
  }
  cpu->tme_debug_cpu_stepping = (steps > 0);
  cpu->tme_debug_cpu_steps = steps;
  cpu->tme_debug_cpu_skip_valid = TRUE;
  cpu->tme_debug_cpu_skip_pc = (*cpu->tme_debug_cpu_pc)(cpu);
  cpu->tme_debug_cpu_stopped = FALSE;
  cpu->tme_debug_cpu_resume_requested = TRUE;
  _tme_debug_cpu_update_slow(cpu);
  (*cpu->tme_debug_cpu_wakeup)(cpu);
}

/* this adds a breakpoint with the given id: */
int
tme_debug_cpu_breakpoint_add(struct tme_debug_cpu *cpu, tme_uint64_t addr, int id)
{
  unsigned int count;

  count = cpu->tme_debug_cpu_breakpoint_count;
  cpu->tme_debug_cpu_breakpoint_addr
    = tme_renew(tme_uint64_t, cpu->tme_debug_cpu_breakpoint_addr, count + 1);
  cpu->tme_debug_cpu_breakpoint_id
    = tme_renew(int, cpu->tme_debug_cpu_breakpoint_id, count + 1);
  cpu->tme_debug_cpu_breakpoint_addr[count] = addr;
  cpu->tme_debug_cpu_breakpoint_id[count] = id;
  cpu->tme_debug_cpu_breakpoint_count = count + 1;
  _tme_debug_cpu_update_slow(cpu);
  return (TME_OK);
}

/* this removes the breakpoint with the given id, returning ENOENT if
   there is none: */
int
tme_debug_cpu_breakpoint_remove(struct tme_debug_cpu *cpu, int id)
{
  unsigned int bp_i;

  for (bp_i = 0; bp_i < cpu->tme_debug_cpu_breakpoint_count; bp_i++) {
    if (cpu->tme_debug_cpu_breakpoint_id[bp_i] == id) {
      cpu->tme_debug_cpu_breakpoint_count--;
      memmove(&cpu->tme_debug_cpu_breakpoint_addr[bp_i],
	      &cpu->tme_debug_cpu_breakpoint_addr[bp_i + 1],
	      sizeof(tme_uint64_t) * (cpu->tme_debug_cpu_breakpoint_count - bp_i));
      memmove(&cpu->tme_debug_cpu_breakpoint_id[bp_i],
	      &cpu->tme_debug_cpu_breakpoint_id[bp_i + 1],
	      sizeof(int) * (cpu->tme_debug_cpu_breakpoint_count - bp_i));
      _tme_debug_cpu_update_slow(cpu);
      return (TME_OK);
    }
  }
  return (ENOENT);
}

/* this sets the function called when a CPU stops: */
void
tme_debug_set_stop_hook(void (*hook) _TME_P((struct tme_debug_cpu *)))
{
  _tme_debug_stop_hook = hook;
}

/* this registers a console: */
void
tme_debug_console_register(struct tme_debug_console *console)
{
  struct tme_debug_console **_prev;

  for (_prev = &_tme_debug_consoles;
       *_prev != NULL;
       _prev = &(*_prev)->tme_debug_console_next);
  console->tme_debug_console_next = NULL;
  console->tme_debug_console_log = NULL;
  console->tme_debug_console_log_total = 0;
  *_prev = console;
}

/* this returns the consoles: */
struct tme_debug_console *
tme_debug_consoles(void)
{
  return (_tme_debug_consoles);
}

/* a console calls this with what the guest has written: */
void
tme_debug_console_output(struct tme_debug_console *console, const tme_uint8_t *data, unsigned int count)
{
  tme_uint64_t total;

  if (console->tme_debug_console_log == NULL) {
    console->tme_debug_console_log = tme_new(tme_uint8_t, TME_DEBUG_CONSOLE_LOG);
  }
  total = console->tme_debug_console_log_total;
  for (; count > 0; count--) {
    console->tme_debug_console_log[total % TME_DEBUG_CONSOLE_LOG] = *(data++);
    total++;
  }
  console->tme_debug_console_log_total = total;
}

/* this copies up to max bytes of a console's output, from offset since
   on (counting from its first byte ever) to buf.  *_start is set to the
   offset of the first byte copied, which is later than since if those
   bytes are no longer kept, and *_total to how many bytes have been
   written.  it returns how many bytes were copied: */
unsigned int
tme_debug_console_read(struct tme_debug_console *console, tme_uint64_t since,
		       tme_uint8_t *buf, unsigned int max,
		       tme_uint64_t *_start, tme_uint64_t *_total)
{
  tme_uint64_t total, oldest;
  unsigned int count;

  total = console->tme_debug_console_log_total;
  oldest = (total > TME_DEBUG_CONSOLE_LOG ? total - TME_DEBUG_CONSOLE_LOG : 0);
  if (since < oldest) {
    since = oldest;
  }
  if (since > total) {
    since = total;
  }
  *_start = since;
  *_total = total;
  for (count = 0; count < max && since + count < total; count++) {
    buf[count] = console->tme_debug_console_log[(since + count) % TME_DEBUG_CONSOLE_LOG];
  }
  return (count);
}

/* this adds a watch, which belongs to us from now on: */
void
tme_debug_watch_add(struct tme_debug_watch *watch)
{
  struct tme_debug_watch **_prev;

  _tme_debug_init();
  tme_mutex_lock(&_tme_debug_mutex);
  if (_tme_debug_trace == NULL) {
    _tme_debug_trace = tme_new0(struct tme_debug_trace_event, TME_DEBUG_TRACE_SIZE);
  }
  tme_mutex_unlock(&_tme_debug_mutex);
  for (_prev = &_tme_debug_watches;
       *_prev != NULL;
       _prev = &(*_prev)->tme_debug_watch_next);
  watch->tme_debug_watch_next = NULL;
  *_prev = watch;
  if (watch->tme_debug_watch_kind == TME_DEBUG_WATCH_EXCEPTION) {
    _tme_debug_watches_exception++;
  }
  else {
    _tme_debug_watches_device++;
  }
}

/* this removes the watch with the given id, returning ENOENT if there
   is none.  NB that with threads that aren't cooperative, a CPU may
   still be looking at it, so it isn't freed: */
int
tme_debug_watch_remove(int id)
{
  struct tme_debug_watch **_prev, *watch;

  for (_prev = &_tme_debug_watches;
       (watch = *_prev) != NULL;
       _prev = &watch->tme_debug_watch_next) {
    if (watch->tme_debug_watch_id == id) {
      if (watch->tme_debug_watch_kind == TME_DEBUG_WATCH_EXCEPTION) {
	_tme_debug_watches_exception--;
      }
      else {
	_tme_debug_watches_device--;
      }
      *_prev = watch->tme_debug_watch_next;
      return (TME_OK);
    }
  }
  return (ENOENT);
}

/* this returns the watches: */
struct tme_debug_watch *
tme_debug_watches(void)
{
  return (_tme_debug_watches);
}

/* this matches a name against a pattern with * and ?, ignoring
   case: */
static int
_tme_debug_glob(const char *pattern, const char *name)
{
  for (; *pattern != '\0'; pattern++, name++) {
    if (*pattern == '*') {
      for (;; name++) {
	if (_tme_debug_glob(pattern + 1, name)) {
	  return (TRUE);
	}
	if (*name == '\0') {
	  return (FALSE);
	}
      }
    }
    if (*name == '\0'
	|| (*pattern != '?'
	    && tolower((unsigned char) *pattern) != tolower((unsigned char) *name))) {
      return (FALSE);
    }
  }
  return (*name == '\0');
}

/* this returns nonzero if an exception watch sees an exception: */
static int
_tme_debug_watch_exception(const struct tme_debug_watch *watch, const char *name, tme_uint32_t code)
{
  unsigned int name_i;
  const char *pattern;
  char *end;

  if (watch->tme_debug_watch_names_count == 0) {
    return (TRUE);
  }
  for (name_i = 0; name_i < watch->tme_debug_watch_names_count; name_i++) {
    pattern = watch->tme_debug_watch_names[name_i];
    if (isdigit((unsigned char) pattern[0])) {
      if (strtoul(pattern, &end, 0) == code && *end == '\0') {
	return (TRUE);
      }
    }
    else if (_tme_debug_glob(pattern, name)) {
      return (TRUE);
    }
  }
  return (FALSE);
}

/* this puts an event into the trace, and stops the CPU if the watch
   that saw it says to: */
static void
_tme_debug_watch_hit(struct tme_debug_cpu *cpu,
		     const struct tme_debug_watch *watch,
		     struct tme_debug_trace_event *event)
{
  struct tme_debug_cpu *cpu_other;
  tme_uint64_t seq;

  /* fill in the rest of the event.  an exception has its PC, and a
     device access is at the CPU's: */
  event->tme_debug_trace_cpu = 0;
  for (cpu_other = _tme_debug_cpus;
       cpu_other != NULL && cpu_other != cpu;
       cpu_other = cpu_other->tme_debug_cpu_next) {
    event->tme_debug_trace_cpu++;
  }
  event->tme_debug_trace_watch = watch->tme_debug_watch_id;
  event->tme_debug_trace_insns
    = (_tme_debug_cpus == NULL
       ? 0
       : (*_tme_debug_cpus->tme_debug_cpu_insns)(_tme_debug_cpus));
  if (event->tme_debug_trace_kind == TME_DEBUG_WATCH_DEVICE && cpu != NULL) {
    event->tme_debug_trace_pc
      = (cpu->tme_debug_cpu_pc_now != NULL
	 ? (*cpu->tme_debug_cpu_pc_now)(cpu)
	 : (*cpu->tme_debug_cpu_pc)(cpu));
  }

  /* put it into the trace: */
  tme_mutex_lock(&_tme_debug_mutex);
  seq = _tme_debug_trace_total++;
  event->tme_debug_trace_seq = seq;
  _tme_debug_trace[seq % TME_DEBUG_TRACE_SIZE] = *event;
  tme_mutex_unlock(&_tme_debug_mutex);

  /* stop the CPU: */
  if (watch->tme_debug_watch_stop
      && cpu != NULL
      && !cpu->tme_debug_cpu_stopped
      && !cpu->tme_debug_cpu_stop_requested) {
    cpu->tme_debug_cpu_watch_hit = watch->tme_debug_watch_id;
    cpu->tme_debug_cpu_watch_seq = seq;
    tme_debug_cpu_stop(cpu, "watch");
  }
}

/* a CPU calls this when it takes an exception named name (a static
   string), with code, at pc, to the handler at vector, with any
   faulting address.  if a watch stops the CPU, the CPU must stop
   before the handler's first instruction: */
void
tme_debug_exception(struct tme_debug_cpu *cpu,
		    const char *name,
		    tme_uint32_t code,
		    tme_uint64_t pc,
		    tme_uint64_t vector,
		    int vaddr_valid,
		    tme_uint64_t vaddr)
{
  const struct tme_debug_watch *watch, *watch_hit;
  struct tme_debug_trace_event event;

  if (__tme_predict_true(_tme_debug_watches_exception == 0)) {
    return;
  }

  /* the first watch that stops, or else the first watch, that sees
     this exception: */
  watch_hit = NULL;
  for (watch = _tme_debug_watches; watch != NULL; watch = watch->tme_debug_watch_next) {
    if (watch->tme_debug_watch_kind == TME_DEBUG_WATCH_EXCEPTION
	&& _tme_debug_watch_exception(watch, name, code)
	&& (watch_hit == NULL
	    || (watch->tme_debug_watch_stop
		&& !watch_hit->tme_debug_watch_stop))) {
      watch_hit = watch;
    }
  }
  if (watch_hit == NULL) {
    return;
  }

  memset(&event, 0, sizeof(event));
  event.tme_debug_trace_kind = TME_DEBUG_WATCH_EXCEPTION;
  event.tme_debug_trace_name = name;
  event.tme_debug_trace_code = code;
  event.tme_debug_trace_vector = vector;
  event.tme_debug_trace_vaddr_valid = vaddr_valid;
  event.tme_debug_trace_vaddr = vaddr;
  event.tme_debug_trace_pc = pc;
  _tme_debug_watch_hit(cpu, watch_hit, &event);
}

/* this copies up to max trace events, from number since on, to
   events.  *_start is set to the number of the first event copied,
   which is later than since if those events are no longer kept, and
   *_total to how many events there have been.  it returns how many
   events were copied: */
unsigned int
tme_debug_trace_read(tme_uint64_t since,
		     struct tme_debug_trace_event *events,
		     unsigned int max,
		     tme_uint64_t *_start,
		     tme_uint64_t *_total)
{
  tme_uint64_t total, oldest;
  unsigned int count;

  _tme_debug_init();
  tme_mutex_lock(&_tme_debug_mutex);
  total = _tme_debug_trace_total;
  oldest = (total > TME_DEBUG_TRACE_SIZE ? total - TME_DEBUG_TRACE_SIZE : 0);
  if (since < oldest) {
    since = oldest;
  }
  if (since > total) {
    since = total;
  }
  *_start = since;
  *_total = total;
  for (count = 0; count < max && since + count < total; count++) {
    events[count] = _tme_debug_trace[(since + count) % TME_DEBUG_TRACE_SIZE];
  }
  tme_mutex_unlock(&_tme_debug_mutex);
  return (count);
}

/* this registers a screen, which can then be saved: */
void
tme_debug_screen_register(struct tme_debug_screen *screen)
{
  struct tme_debug_screen **_prev;

  _tme_debug_init();
  for (_prev = &_tme_debug_screens; *_prev != NULL; _prev = &(*_prev)->tme_debug_screen_next);
  screen->tme_debug_screen_next = NULL;
  *_prev = screen;
}

/* this returns the first screen: */
struct tme_debug_screen *
tme_debug_screens(void)
{
  return (_tme_debug_screens);
}

/* this registers a bus, whose devices are then listed, and watched: */
void
tme_debug_bus_register(struct tme_element *element, struct tme_bus *bus)
{
  struct _tme_debug_bus *debug_bus;

  _tme_debug_init();
  debug_bus = tme_new0(struct _tme_debug_bus, 1);
  debug_bus->_tme_debug_bus_element = element;
  debug_bus->_tme_debug_bus_bus = bus;
  debug_bus->_tme_debug_bus_next = _tme_debug_buses;
  _tme_debug_buses = debug_bus;
}

/* this copies up to max regions of the devices on the buses to
   regions, returning how many there are: */
unsigned int
tme_debug_regions(struct tme_debug_region *regions, unsigned int max)
{
  const struct _tme_debug_bus *debug_bus;
  const struct tme_bus *bus;
  const struct tme_bus_connection_int *conn_int;
  const struct tme_bus_subregion *subregion;
  unsigned int count;
  int addressable_i;

  count = 0;
  for (debug_bus = _tme_debug_buses;
       debug_bus != NULL;
       debug_bus = debug_bus->_tme_debug_bus_next) {
    bus = debug_bus->_tme_debug_bus_bus;
    for (addressable_i = 0;
	 addressable_i < bus->tme_bus_addressables_count;
	 addressable_i++, count++) {
      if (count >= max) {
	continue;
      }
      conn_int = bus->tme_bus_addressables[addressable_i].tme_bus_addressable_connection;
      subregion = bus->tme_bus_addressables[addressable_i].tme_bus_addressable_subregion;
      regions[count].tme_debug_region_element
	= conn_int->tme_bus_connection_int.tme_bus_connection.tme_connection_other->tme_connection_element;
      regions[count].tme_debug_region_bus = debug_bus->_tme_debug_bus_element;
      regions[count].tme_debug_region_address = conn_int->tme_bus_connection_int_address;
      regions[count].tme_debug_region_first = subregion->tme_bus_subregion_address_first;
      regions[count].tme_debug_region_last = subregion->tme_bus_subregion_address_last;
    }
  }
  return (count);
}

/* this copies up to the first eight bytes of a bus cycle's buffer: */
static unsigned int
_tme_debug_cycle_data(const tme_uint8_t *buffer, int increment, unsigned int size, tme_uint8_t *data)
{
  unsigned int count;

  for (count = 0; count < size && count < 8; count++) {
    data[count] = buffer[(int) count * increment];
  }
  return (count);
}

/* this is the cycle handler of TLB entries for devices.  it calls the
   device's own, and watches the cycle: */
static int
_tme_debug_cycle(void *_debug_cycle, struct tme_bus_cycle *cycle)
{
  const struct _tme_debug_cycle *debug_cycle;
  const struct tme_debug_watch *watch, *watch_hit;
  struct tme_debug_trace_event event;
  tme_uint64_t offset, offset_last;
  unsigned int access, size, size_done;
  const tme_uint8_t *buffer;
  int increment;
  tme_uint8_t data[8];
  unsigned int data_count;
  int rc;

  debug_cycle = (const struct _tme_debug_cycle *) _debug_cycle;

  /* if no device is watched, just do the cycle: */
  if (__tme_predict_true(_tme_debug_watches_device == 0)) {
    return ((*debug_cycle->_tme_debug_cycle_handler)
	    (debug_cycle->_tme_debug_cycle_private, cycle));
  }

  /* get the cycle, and the bytes it writes.  NB that a cycle handler
     advances the cycle's address and buffer past what it
     transferred: */
  offset = cycle->tme_bus_cycle_address;
  buffer = cycle->tme_bus_cycle_buffer;
  increment = cycle->tme_bus_cycle_buffer_increment;
  access = ((cycle->tme_bus_cycle_type & TME_BUS_CYCLE_WRITE)
	    ? TME_DEBUG_ACCESS_WRITE
	    : TME_DEBUG_ACCESS_READ);
  size = cycle->tme_bus_cycle_size;
  data_count = 0;
  if (access == TME_DEBUG_ACCESS_WRITE) {
    data_count = _tme_debug_cycle_data(buffer, increment, size, data);
  }

  /* do the cycle: */
  rc = ((*debug_cycle->_tme_debug_cycle_handler)
	(debug_cycle->_tme_debug_cycle_private, cycle));

  /* the transfer may have been smaller: */
  size_done = cycle->tme_bus_cycle_size;
  if (rc == TME_OK && size_done > 0 && size_done < size) {
    size = size_done;
    data_count = TME_MIN(data_count, size);
  }

  /* the first watch that stops, or else the first watch, that sees
     this cycle: */
  offset_last = offset + (size > 0 ? size - 1 : 0);
  watch_hit = NULL;
  for (watch = _tme_debug_watches; watch != NULL; watch = watch->tme_debug_watch_next) {
    if (watch->tme_debug_watch_kind == TME_DEBUG_WATCH_DEVICE
	&& watch->tme_debug_watch_element == debug_cycle->_tme_debug_cycle_element
	&& (watch->tme_debug_watch_access & access)
	&& offset <= watch->tme_debug_watch_offset_last
	&& offset_last >= watch->tme_debug_watch_offset_first
	&& (watch_hit == NULL
	    || (watch->tme_debug_watch_stop
		&& !watch_hit->tme_debug_watch_stop))) {
      watch_hit = watch;
    }
  }
  if (watch_hit == NULL) {
    return (rc);
  }

  /* the bytes read: */
  if (access == TME_DEBUG_ACCESS_READ && rc == TME_OK) {
    data_count = _tme_debug_cycle_data(buffer, increment, size, data);
  }

  memset(&event, 0, sizeof(event));
  event.tme_debug_trace_kind = TME_DEBUG_WATCH_DEVICE;
  event.tme_debug_trace_element = debug_cycle->_tme_debug_cycle_element;
  event.tme_debug_trace_offset = offset;
  event.tme_debug_trace_access = access;
  event.tme_debug_trace_len = size;
  event.tme_debug_trace_data_count = data_count;
  memcpy(event.tme_debug_trace_data, data, data_count);
  event.tme_debug_trace_errno = rc;

  /* NB: this is taken to be the first CPU's access: */
  _tme_debug_watch_hit(_tme_debug_cpus, watch_hit, &event);
  return (rc);
}

/* a bus calls this after a device has filled a TLB entry, so the
   device's cycles can be watched: */
void
tme_debug_bus_tlb(struct tme_element *element, struct tme_bus_tlb *tlb)
{
  struct _tme_debug_cycle *debug_cycle;

  /* if the cycle handler is already watched (for a device on another
     bus, which is a device on this one), or there is none, there is
     nothing to do: */
  if (tlb->tme_bus_tlb_cycle == _tme_debug_cycle
      || tlb->tme_bus_tlb_cycle == NULL) {
    return;
  }

  /* find or make our cycle handler for this device's: */
  tme_mutex_lock(&_tme_debug_mutex);
  for (debug_cycle = _tme_debug_cycles;
       debug_cycle != NULL;
       debug_cycle = debug_cycle->_tme_debug_cycle_next) {
    if (debug_cycle->_tme_debug_cycle_handler == tlb->tme_bus_tlb_cycle
	&& debug_cycle->_tme_debug_cycle_private == tlb->tme_bus_tlb_cycle_private
	&& debug_cycle->_tme_debug_cycle_element == element) {
      break;
    }
  }
  if (debug_cycle == NULL
      && _tme_debug_cycles_count < _TME_DEBUG_CYCLES_MAX) {
    debug_cycle = tme_new0(struct _tme_debug_cycle, 1);
    debug_cycle->_tme_debug_cycle_element = element;
    debug_cycle->_tme_debug_cycle_handler = tlb->tme_bus_tlb_cycle;
    debug_cycle->_tme_debug_cycle_private = tlb->tme_bus_tlb_cycle_private;
    debug_cycle->_tme_debug_cycle_next = _tme_debug_cycles;
    _tme_debug_cycles = debug_cycle;
    _tme_debug_cycles_count++;
  }
  tme_mutex_unlock(&_tme_debug_mutex);

  /* use it: */
  if (debug_cycle != NULL) {
    tlb->tme_bus_tlb_cycle = _tme_debug_cycle;
    tlb->tme_bus_tlb_cycle_private = debug_cycle;
  }
}

/* this returns the device's own cycle handler of a TLB entry, which
   may be ours: */
tme_bus_cycle_handler
tme_debug_bus_tlb_cycle(const struct tme_bus_tlb *tlb)
{
  if (tlb->tme_bus_tlb_cycle == _tme_debug_cycle) {
    return (((const struct _tme_debug_cycle *) tlb->tme_bus_tlb_cycle_private)->_tme_debug_cycle_handler);
  }
  return (tlb->tme_bus_tlb_cycle);
}
