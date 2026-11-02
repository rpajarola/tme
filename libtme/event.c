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
#ifndef WIN32
#include <poll.h>
#endif /* !WIN32 */
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

#ifdef WIN32

/* Windows' poll(), WSAPoll(), works only with sockets, and takes no
   POLLPRI.  pipes, consoles and files are checked here instead: a
   pipe is readable when it holds data or has been closed, a console
   when a key that reads as a character is waiting (in line mode,
   when a whole line is), and a file always.  everything is writable.
   (a console in raw mode is better read through
   tme_win32_stdin_pump(), which gets escape sequences right.)
   while a pipe or a console is waited on, this checks every few
   milliseconds; with only sockets, WSAPoll() does all the waiting: */

#define _TME_EVENT_WIN32_STEP	(10)

/* this returns nonzero if a non-socket handle is ready for
   reading: */
static int
_tme_event_win32_readable(HANDLE handle)
{
  DWORD avail, mode, count, i;
  INPUT_RECORD records[64];
  const KEY_EVENT_RECORD *key;

  switch (GetFileType(handle)) {

  case FILE_TYPE_PIPE:
    /* a pipe that can't be peeked at (Wine's, for a Unix pipe) is never
       readable here.  tme_win32_stdin_pump() puts standard input behind
       a pipe that can be: */
    if (!PeekNamedPipe(handle, NULL, 0, NULL, &avail, NULL)) {
      return (GetLastError() == ERROR_BROKEN_PIPE);
    }
    return (avail > 0);

  case FILE_TYPE_CHAR:
    if (!GetConsoleMode(handle, &mode)) {
      /* a character device that isn't a console, like NUL: */
      return (TRUE);
    }
    for (;;) {
      if (!PeekConsoleInputW(handle, records, TME_ARRAY_ELS(records), &count)
	  || count == 0) {
	return (FALSE);
      }
      for (i = 0; i < count; i++) {
	key = &records[i].Event.KeyEvent;
	if (records[i].EventType == KEY_EVENT
	    && key->bKeyDown
	    && key->uChar.UnicodeChar != 0
	    && (!(mode & ENABLE_LINE_INPUT)
		|| key->uChar.UnicodeChar == '\r')) {
	  return (TRUE);
	}
      }

      /* in line mode, the keys typed so far are part of a line that
	 isn't finished: */
      if (mode & ENABLE_LINE_INPUT) {
	return (FALSE);
      }

      /* otherwise, these are key releases, mouse and focus events
	 and the like, which a read() would block on, so drop them: */
      ReadConsoleInputW(handle, records, count, &count);
    }

  default:
    return (TRUE);
  }
}

/* this does what poll() does, for any mix of sockets and other
   handles: */
static int
_tme_event_poll(struct pollfd *fds, int nfds, int timeout)
{
  WSAPOLLFD *sockets;
  int *socket_fds;
  int n_sockets, n_waiting, ready, i, j, step, type;
  int type_len;
  DWORD start;
  HANDLE handle;

  sockets = tme_new(WSAPOLLFD, TME_MAX(nfds, 1));
  socket_fds = tme_new(int, TME_MAX(nfds, 1));
  start = GetTickCount();
  for (;;) {

    /* check the handles that aren't sockets, and collect the
       sockets: */
    ready = 0;
    n_sockets = 0;
    n_waiting = 0;
    for (i = 0; i < nfds; i++) {
      fds[i].revents = 0;
      type_len = sizeof(type);
      if (getsockopt((SOCKET) fds[i].fd, SOL_SOCKET, SO_TYPE, (char *) &type, &type_len) == 0) {
	sockets[n_sockets].fd = (SOCKET) fds[i].fd;
	sockets[n_sockets].events = (((fds[i].events & POLLIN) ? POLLRDNORM : 0)
				     | ((fds[i].events & POLLOUT) ? POLLWRNORM : 0));
	sockets[n_sockets].revents = 0;
	socket_fds[n_sockets] = i;
	n_sockets++;
	continue;
      }
      handle = (HANDLE) _get_osfhandle(fds[i].fd);
      if (handle == INVALID_HANDLE_VALUE) {
	fds[i].revents = POLLNVAL;
      }
      else {
	if (fds[i].events & POLLIN) {
	  if (_tme_event_win32_readable(handle)) {
	    fds[i].revents |= POLLIN;
	  }
	  else {
	    n_waiting++;
	  }
	}
	fds[i].revents |= fds[i].events & POLLOUT;
      }
      if (fds[i].revents != 0) {
	ready++;
      }
    }

    /* wait for the sockets, but only for a step if something else is
       ready or is being waited on: */
    step = timeout;
    if (ready > 0) {
      step = 0;
    }
    else if (n_waiting > 0
	     && (step < 0 || step > _TME_EVENT_WIN32_STEP)) {
      step = _TME_EVENT_WIN32_STEP;
    }
    if (n_sockets > 0) {
      if (WSAPoll(sockets, n_sockets, step) < 0) {
	ready = -1;
	break;
      }
      for (j = 0; j < n_sockets; j++) {
	i = socket_fds[j];
	if (sockets[j].revents & (POLLRDNORM | POLLRDBAND)) {
	  fds[i].revents |= POLLIN;
	}
	if (sockets[j].revents & POLLWRNORM) {
	  fds[i].revents |= POLLOUT;
	}
	fds[i].revents |= sockets[j].revents & (POLLERR | POLLHUP | POLLNVAL);
	if (fds[i].revents != 0) {
	  ready++;
	}
      }
    }
    else if (step != 0) {
      Sleep(step < 0 ? INFINITE : (DWORD) step);
    }

    if (ready > 0
	|| timeout == 0
	|| (timeout > 0 && GetTickCount() - start >= (DWORD) timeout)) {
      break;
    }
  }
  tme_free(sockets);
  tme_free(socket_fds);
  return (ready);
}
#define poll _tme_event_poll

#endif /* WIN32 */

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
