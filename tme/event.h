/* tme/event.h - header file for file descriptor event sets: */

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

#ifndef _TME_EVENT_H
#define _TME_EVENT_H

#include <tme/common.h>

/* includes: */
#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* an event set is a set of file descriptors to wait on with poll(2).
   this replaces the event layer previously borrowed from OpenVPN,
   and keeps its interface: */

/* the conditions to wait for: */
#define EVENT_READ		(1 << 0)
#define EVENT_WRITE		(1 << 1)

/* event set flags.  EVENT_METHOD_FAST sets never look up an event
   that is already in the set, so each event must be added only once
   between resets, and events can't be deleted: */
#define EVENT_METHOD_US_TIMEOUT	(1 << 0)
#define EVENT_METHOD_FAST	(1 << 1)

/* an event is a file descriptor: */
typedef int event_t;
#define UNDEFINED_EVENT		(-1)

/* the timeout, in seconds, used to wait for "ever": */
#define BIG_TIMEOUT		(60 * 60 * 24 * 7)

/* an event that is ready: */
struct event_set_return {
  unsigned int rwflags;
  void *arg;
};

struct event_set;

/* prototypes: */
struct event_set *event_set_init _TME_P((int *maxevents, unsigned int flags));
void event_free _TME_P((struct event_set *es));
void event_reset _TME_P((struct event_set *es));
void event_del _TME_P((struct event_set *es, event_t event));
void event_ctl _TME_P((struct event_set *es, event_t event, unsigned int rwflags, void *arg));

/* this waits for events.  it returns -1 on error, zero on timeout,
   or the number of ready events stored in out: */
int event_wait _TME_P((struct event_set *es, const struct timeval *tv, struct event_set_return *out, int outlen));

#endif /* !_TME_EVENT_H */
