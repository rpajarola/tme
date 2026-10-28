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
#include <errno.h>
#include <string.h>

/* the CPUs and the consoles, in the order registered: */
static struct tme_debug_cpu *_tme_debug_cpus;
static struct tme_debug_console *_tme_debug_consoles;

/* this is called when a CPU stops: */
static void (*_tme_debug_stop_hook) _TME_P((struct tme_debug_cpu *));

/* this names a CPU or console, after tmesh has made its element: */
void
tme_debug_name(struct tme_element *element, const char *name)
{
  struct tme_debug_cpu *cpu;
  struct tme_debug_console *console;

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
