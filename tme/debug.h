/* tme/debug.h - public header file for debugging CPUs and consoles: */

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

#ifndef _TME_DEBUG_H
#define _TME_DEBUG_H

#include <tme/common.h>

/* includes: */
#include <tme/element.h>

/* CPUs and consoles that a debugger (tmesh's control socket) can use.

   a CPU registers itself, and checks tme_debug_cpu_slow often: while
   it is nonzero, the CPU calls tme_debug_cpu_check() before each
   instruction, and stops if it returns nonzero.  a stopped CPU calls
   tme_debug_cpu_stopped() once, and then waits (its stop function's
   wakeups) until tme_debug_cpu_resuming() returns nonzero.

   while a CPU is stopped, or (with cooperative threads) while the
   debugger runs at all, the debugger reads and writes the CPU's
   registers directly with the CPU's functions.

   a console registers itself, and gives the debugger what the guest
   writes to it; the debugger gives it what to type: */

/* a register, as a CPU lists them: */
struct tme_debug_reg {

  /* the register's name: */
  _tme_const char *tme_debug_reg_name;

  /* its size in bits: */
  unsigned int tme_debug_reg_bits;

  /* its value: */
  tme_uint64_t tme_debug_reg_value;
};

/* a CPU that can be debugged: */
struct tme_debug_cpu {

  /* the next CPU: */
  struct tme_debug_cpu *tme_debug_cpu_next;

  /* the CPU's element, and the name tmesh gave it: */
  struct tme_element *tme_debug_cpu_element;
  char *tme_debug_cpu_name;

  /* the architecture ("m68k"), the model ("m68020"), the address
     size, and the byte order: */
  _tme_const char *tme_debug_cpu_arch;
  _tme_const char *tme_debug_cpu_model;
  unsigned int tme_debug_cpu_bits;
  int tme_debug_cpu_big_endian;

  /* the CPU's private state: */
  void *tme_debug_cpu_private;

  /* this wakes the CPU up, so that it looks at a stop or resume
     request soon: */
  void (*tme_debug_cpu_wakeup) _TME_P((struct tme_debug_cpu *));

  /* this gets the register numbered reg_i, returning nonzero, or
     returns zero if there is no such register: */
  int (*tme_debug_cpu_reg) _TME_P((struct tme_debug_cpu *, unsigned int, struct tme_debug_reg *));

  /* this sets the register named, returning zero if there is no such
     register: */
  int (*tme_debug_cpu_reg_set) _TME_P((struct tme_debug_cpu *, _tme_const char *, tme_uint64_t));

  /* this reads (or with nonzero write, writes) count bytes of memory
     at an address, in an address space (on the m68k a function code,
     on the SPARC an ASI), or in the CPU's usual data space if the
     space is negative.  only memory is accessed, never device
     registers.  it returns how many bytes it accessed, up to the first
     that it couldn't: */
  unsigned int (*tme_debug_cpu_mem) _TME_P((struct tme_debug_cpu *, tme_uint64_t, tme_uint8_t *, unsigned int, int, int));

  /* this returns the PC: */
  tme_uint64_t (*tme_debug_cpu_pc) _TME_P((struct tme_debug_cpu *));

  /* this returns about how many instructions have run: */
  tme_uint64_t (*tme_debug_cpu_insns) _TME_P((struct tme_debug_cpu *));

  /* if not NULL, this returns the PC of the instruction running now,
     when that isn't the PC (for the trace): */
  tme_uint64_t (*tme_debug_cpu_pc_now) _TME_P((struct tme_debug_cpu *));

  /* the rest is kept by debug.c: */

  /* nonzero while the CPU must call tme_debug_cpu_check() before
     each instruction: */
  volatile int tme_debug_cpu_slow;

  /* nonzero while a stop has been asked for, and why: */
  volatile int tme_debug_cpu_stop_requested;
  _tme_const char *tme_debug_cpu_stop_reason;

  /* nonzero while the CPU is stopped, and while it has been asked to
     resume: */
  volatile int tme_debug_cpu_stopped;
  volatile int tme_debug_cpu_resume_requested;

  /* while stepping, the number of instructions still to run: */
  int tme_debug_cpu_stepping;
  unsigned long tme_debug_cpu_steps;

  /* a breakpoint at this PC is passed over once, when resuming: */
  int tme_debug_cpu_skip_valid;
  tme_uint64_t tme_debug_cpu_skip_pc;

  /* the breakpoints: */
  unsigned int tme_debug_cpu_breakpoint_count;
  tme_uint64_t *tme_debug_cpu_breakpoint_addr;
  int *tme_debug_cpu_breakpoint_id;

  /* the breakpoint that was hit, or zero: */
  int tme_debug_cpu_breakpoint_hit;

  /* the watch that stopped the CPU, or zero, and the trace event of
     what it saw: */
  int tme_debug_cpu_watch_hit;
  tme_uint64_t tme_debug_cpu_watch_seq;
};

/* what a watch watches: */
#define TME_DEBUG_WATCH_EXCEPTION	(0)
#define TME_DEBUG_WATCH_DEVICE		(1)

/* device accesses: */
#define TME_DEBUG_ACCESS_READ		(1 << 0)
#define TME_DEBUG_ACCESS_WRITE		(1 << 1)

/* a watch, on CPU exceptions, or on accesses to a device's registers.
   what a watch sees goes into the trace, and stops the machine if the
   watch says so: */
struct tme_debug_watch {

  /* the next watch: */
  struct tme_debug_watch *tme_debug_watch_next;

  /* the watch's id, what it watches, and nonzero if it stops the
     machine: */
  int tme_debug_watch_id;
  unsigned int tme_debug_watch_kind;
  int tme_debug_watch_stop;

  /* for exceptions, the names to watch, or none for any.  a name can
     have * and ? wildcards, or be a number, for an exception code: */
  char **tme_debug_watch_names;
  unsigned int tme_debug_watch_names_count;

  /* for devices, the device, the first and last offsets of its
     registers to watch, and the accesses: */
  struct tme_element *tme_debug_watch_element;
  tme_uint64_t tme_debug_watch_offset_first;
  tme_uint64_t tme_debug_watch_offset_last;
  unsigned int tme_debug_watch_access;
};

/* something a watch saw: */
struct tme_debug_trace_event {

  /* the event's number, starting from zero, and about how many
     instructions the first CPU had run: */
  tme_uint64_t tme_debug_trace_seq;
  tme_uint64_t tme_debug_trace_insns;

  /* the CPU (its number), the watch, what the watch watches, and the
     PC of the instruction: */
  int tme_debug_trace_cpu;
  int tme_debug_trace_watch;
  unsigned int tme_debug_trace_kind;
  tme_uint64_t tme_debug_trace_pc;

  /* for exceptions, the name and code, the address of the handler,
     and any faulting address: */
  _tme_const char *tme_debug_trace_name;
  tme_uint32_t tme_debug_trace_code;
  tme_uint64_t tme_debug_trace_vector;
  int tme_debug_trace_vaddr_valid;
  tme_uint64_t tme_debug_trace_vaddr;

  /* for device accesses, the device, the offset, the access, its
     size, the first bytes read or written, and any error: */
  struct tme_element *tme_debug_trace_element;
  tme_uint64_t tme_debug_trace_offset;
  unsigned int tme_debug_trace_access;
  unsigned int tme_debug_trace_len;
  unsigned int tme_debug_trace_data_count;
  tme_uint8_t tme_debug_trace_data[8];
  int tme_debug_trace_errno;
};

/* how many trace events are kept: */
#define TME_DEBUG_TRACE_SIZE	(32768)

/* a region of a device on a bus: */
struct tme_debug_region {

  /* the device, and the bus: */
  struct tme_element *tme_debug_region_element;
  struct tme_element *tme_debug_region_bus;

  /* the device's address on the bus, and the first and last offsets
     of the region: */
  tme_uint64_t tme_debug_region_address;
  tme_uint64_t tme_debug_region_first;
  tme_uint64_t tme_debug_region_last;
};

/* a console that can be read and typed into: */
struct tme_debug_console {

  /* the next console: */
  struct tme_debug_console *tme_debug_console_next;

  /* the console's element, and the name tmesh gave it: */
  struct tme_element *tme_debug_console_element;
  char *tme_debug_console_name;

  /* the console's private state: */
  void *tme_debug_console_private;

  /* this types characters into the console, returning how many it
     took: */
  unsigned int (*tme_debug_console_input) _TME_P((struct tme_debug_console *, _tme_const tme_uint8_t *, unsigned int));

  /* the rest is kept by debug.c: */

  /* the last TME_DEBUG_CONSOLE_LOG bytes of output, and how many
     have been written: */
  tme_uint8_t *tme_debug_console_log;
  tme_uint64_t tme_debug_console_log_total;
};

/* how much console output is kept: */
#define TME_DEBUG_CONSOLE_LOG	(1 << 20)

/* prototypes: */
struct tme_bus;
struct tme_bus_tlb;
struct tme_bus_cycle;

void tme_debug_name _TME_P((struct tme_element *, _tme_const char *));
_tme_const char *tme_debug_element_name _TME_P((struct tme_element *));
struct tme_element *tme_debug_element_find _TME_P((_tme_const char *));

void tme_debug_cpu_register _TME_P((struct tme_debug_cpu *));
struct tme_debug_cpu *tme_debug_cpus _TME_P((void));
int tme_debug_cpu_check _TME_P((struct tme_debug_cpu *, tme_uint64_t));
void tme_debug_cpu_stopped _TME_P((struct tme_debug_cpu *));
int tme_debug_cpu_resuming _TME_P((struct tme_debug_cpu *));
void tme_debug_cpu_stop _TME_P((struct tme_debug_cpu *, _tme_const char *));
void tme_debug_cpu_resume _TME_P((struct tme_debug_cpu *, unsigned long));
int tme_debug_cpu_breakpoint_add _TME_P((struct tme_debug_cpu *, tme_uint64_t, int));
int tme_debug_cpu_breakpoint_remove _TME_P((struct tme_debug_cpu *, int));
void tme_debug_set_stop_hook _TME_P((void (*) _TME_P((struct tme_debug_cpu *))));

void tme_debug_console_register _TME_P((struct tme_debug_console *));
struct tme_debug_console *tme_debug_consoles _TME_P((void));
void tme_debug_console_output _TME_P((struct tme_debug_console *, _tme_const tme_uint8_t *, unsigned int));
unsigned int tme_debug_console_read _TME_P((struct tme_debug_console *, tme_uint64_t, tme_uint8_t *, unsigned int, tme_uint64_t *, tme_uint64_t *));

void tme_debug_watch_add _TME_P((struct tme_debug_watch *));
int tme_debug_watch_remove _TME_P((int));
struct tme_debug_watch *tme_debug_watches _TME_P((void));
void tme_debug_exception _TME_P((struct tme_debug_cpu *, _tme_const char *, tme_uint32_t, tme_uint64_t, tme_uint64_t, int, tme_uint64_t));
unsigned int tme_debug_trace_read _TME_P((tme_uint64_t, struct tme_debug_trace_event *, unsigned int, tme_uint64_t *, tme_uint64_t *));

void tme_debug_bus_register _TME_P((struct tme_element *, struct tme_bus *));
void tme_debug_bus_tlb _TME_P((struct tme_element *, struct tme_bus_tlb *));
int (*tme_debug_bus_tlb_cycle _TME_P((_tme_const struct tme_bus_tlb *))) _TME_P((void *, struct tme_bus_cycle *));
unsigned int tme_debug_regions _TME_P((struct tme_debug_region *, unsigned int));

#endif /* !_TME_DEBUG_H */
