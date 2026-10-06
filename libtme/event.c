/* libtme/event.c - file descriptor event sets: */

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

/* includes: */
#include <tme/event.h>
#include <tme/misc.h>
#include <poll.h>
#include <errno.h>

/* an event set: */
struct event_set {

  /* nonzero if this is an EVENT_METHOD_FAST set: */
  int fast;

  /* the number of events in the set, and the space allocated: */
  int n_events;
  int capacity;

  /* the events, and their callers' arguments: */
  struct pollfd *events;
  void **args;
};

/* this converts a timeval into a poll(2) timeout: */
static int
_tme_event_timeout(const struct timeval *tv)
{
  long ms;

  if (tv == NULL) {
    return (-1);
  }
  if (tv->tv_sec >= BIG_TIMEOUT) {
    return (BIG_TIMEOUT * 1000);
  }
  ms = (tv->tv_sec * 1000) + ((tv->tv_usec + 999) / 1000);
  return (ms);
}

/* this sets the poll(2) events for the given rwflags: */
static void
_tme_event_pollfd_set(struct pollfd *pfd, unsigned int rwflags)
{
  pfd->events = 0;
  if (rwflags & EVENT_READ) {
    pfd->events |= (POLLIN | POLLPRI);
  }
  if (rwflags & EVENT_WRITE) {
    pfd->events |= POLLOUT;
  }
}

/* this appends an event to a set: */
static void
_tme_event_append(struct event_set *es, event_t event, unsigned int rwflags, void *arg)
{
  if (es->n_events == es->capacity) {
    es->capacity *= 2;
    es->events = tme_renew(struct pollfd, es->events, es->capacity);
    es->args = tme_renew(void *, es->args, es->capacity);
  }
  es->events[es->n_events].fd = event;
  es->events[es->n_events].revents = 0;
  _tme_event_pollfd_set(&es->events[es->n_events], rwflags);
  es->args[es->n_events] = arg;
  es->n_events++;
}

/* this makes a new event set: */
struct event_set *
event_set_init(int *maxevents, unsigned int flags)
{
  struct event_set *es;

  if (*maxevents < 1) {
    *maxevents = 1;
  }
  es = tme_new0(struct event_set, 1);
  es->fast = (flags & EVENT_METHOD_FAST) != 0;
  es->n_events = 0;
  es->capacity = *maxevents;
  es->events = tme_new0(struct pollfd, es->capacity);
  es->args = tme_new0(void *, es->capacity);
  return (es);
}

/* this frees an event set: */
void
event_free(struct event_set *es)
{
  if (es != NULL) {
    tme_free(es->events);
    tme_free(es->args);
    tme_free(es);
  }
}

/* this removes all events from a set: */
void
event_reset(struct event_set *es)
{
  es->n_events = 0;
}

/* this removes an event from a set: */
void
event_del(struct event_set *es, event_t event)
{
  int i;

  assert (!es->fast);
  for (i = 0; i < es->n_events; i++) {
    if (es->events[i].fd == event) {
      es->n_events--;
      memmove(&es->events[i], &es->events[i + 1], sizeof(es->events[0]) * (es->n_events - i));
      memmove(&es->args[i], &es->args[i + 1], sizeof(es->args[0]) * (es->n_events - i));
      break;
    }
  }
}

/* this adds an event to a set, or changes an event already in it: */
void
event_ctl(struct event_set *es, event_t event, unsigned int rwflags, void *arg)
{
  int i;

  if (!es->fast) {
    for (i = 0; i < es->n_events; i++) {
      if (es->events[i].fd == event) {
	_tme_event_pollfd_set(&es->events[i], rwflags);
	es->args[i] = arg;
	return;
      }
    }
  }
  _tme_event_append(es, event, rwflags, arg);
}

/* this waits for events in a set: */
int
event_wait(struct event_set *es, const struct timeval *tv, struct event_set_return *out, int outlen)
{
  int rc;
  int i, j;
  short revents;

  rc = poll(es->events, es->n_events, _tme_event_timeout(tv));
  if (rc <= 0) {
    return (rc);
  }

  /* errors and hangups are reported as readable, so that the
     caller's next read returns them: */
  for (i = j = 0; i < es->n_events && j < outlen; i++) {
    revents = es->events[i].revents;
    if (revents == 0) {
      continue;
    }
    out[j].rwflags = 0;
    if (revents & (POLLIN | POLLPRI | POLLERR | POLLHUP | POLLNVAL)) {
      out[j].rwflags |= EVENT_READ;
    }
    if (revents & POLLOUT) {
      out[j].rwflags |= EVENT_WRITE;
    }
    out[j].arg = es->args[i];
    j++;
  }
  return (j);
}
