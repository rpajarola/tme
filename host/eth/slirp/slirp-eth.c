/* host/eth/slirp/slirp-eth.c - user-mode networking with libslirp: */

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

/* this connects an emulated Ethernet interface to libslirp, the
   user-mode TCP/IP stack that QEMU (-netdev user) uses.  the
   emulated machine is behind a NAT on a private network, by default
   10.0.2.0/24, where slirp is the gateway (10.0.2.2), a DNS
   forwarder (10.0.2.3) and a DHCP server (from 10.0.2.15).  the
   machine's TCP and UDP go out through ordinary sockets on the host,
   so this needs no privileges, and nothing on the host is
   configured.  ICMP echo goes out too where the host allows
   unprivileged ICMP sockets.  "forward" makes host ports reach ports
   on the machine.

   slirp isn't thread-safe.  everything that touches it is done with
   the Ethernet mutex held: frames from the machine are given to it
   in _tme_slirp_write, and the reader thread runs its poll loop in
   _tme_slirp_read.  frames that slirp makes for the machine are
   queued, and handed to the Ethernet one at a time by
   _tme_slirp_read.

   the reader waits, with the mutex unlocked, for slirp's sockets and
   for a wakeup pipe, which _tme_slirp_write and slirp's notify
   callback write to, so that frames for the machine and new sockets
   are seen at once.  if a frame from the machine was given to slirp
   during the wait, slirp's sockets may have changed, so the events
   are thrown away and the wait is redone.  with cooperative
   threads, a wait that blocks starts _tme_slirp_read over, so it
   keeps nothing on its stack across one: */

#include <tme/common.h>

/* includes: */
#include "eth-if.h"
#include <tme/socket.h>
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "libslirp/libslirp.h"

/* macros: */

/* the most frames queued for the machine.  slirp can make a TCP
   window's worth at once; a frame that doesn't fit is dropped, and
   TCP sends it again: */
#define TME_SLIRP_QUEUE_MAX	(256)

/* the most port forwards: */
#define TME_SLIRP_FORWARDS_MAX	(32)

/* the longest wait, in milliseconds: */
#define TME_SLIRP_WAIT_MAX	(1000)

/* the smallest frame we pass on, without the CRC: */
#define TME_SLIRP_FRAME_MIN	(TME_ETHERNET_FRAME_MIN - TME_ETHERNET_CRC_SIZE)

/* structures: */

/* a frame queued for the machine: */
struct tme_slirp_frame {
  size_t tme_slirp_frame_len;
  tme_uint8_t tme_slirp_frame_bytes[TME_ETHERNET_FRAME_MAX];
};

/* a descriptor that slirp polls: */
struct tme_slirp_pollfd {
  int tme_slirp_pollfd_fd;
  int tme_slirp_pollfd_events;
  int tme_slirp_pollfd_revents;
};

/* a slirp timer: */
struct tme_slirp_timer {
  struct tme_slirp_timer *tme_slirp_timer_next;
  SlirpTimerCb tme_slirp_timer_cb;
  void *tme_slirp_timer_opaque;

  /* nonzero if the timer is set, and when it expires, in
     milliseconds: */
  int tme_slirp_timer_armed;
  tme_int64_t tme_slirp_timer_expire;
};

/* a port forward: */
struct tme_slirp_forward {
  int tme_slirp_forward_udp;
  struct in_addr tme_slirp_forward_host_addr;
  int tme_slirp_forward_host_port;
  struct in_addr tme_slirp_forward_guest_addr;
  int tme_slirp_forward_guest_port;
};

/* our private data: */
struct tme_slirp {

  /* backpointer to our element: */
  struct tme_element *tme_slirp_element;

  /* our Ethernet: */
  struct tme_ethernet *tme_slirp_eth;

  /* slirp: */
  Slirp *tme_slirp_slirp;

  /* the wakeup pipe: */
  int tme_slirp_wakeup[2];

  /* this is incremented whenever a frame from the machine is given to
     slirp: */
  unsigned int tme_slirp_input_count;

  /* the frame buffers: */
  tme_uint8_t tme_slirp_buffer_in[TME_ETHERNET_FRAME_MAX];
  tme_uint8_t tme_slirp_buffer_out[TME_ETHERNET_FRAME_MAX];

  /* the frames queued for the machine, as a ring: */
  struct tme_slirp_frame *tme_slirp_queue;
  unsigned int tme_slirp_queue_head;
  unsigned int tme_slirp_queue_count;

  /* nonzero after we warned that the queue overflowed: */
  int tme_slirp_queue_warned;

  /* the descriptors that slirp polls, and the events from a wait: */
  struct tme_slirp_pollfd *tme_slirp_pollfds;
  unsigned int tme_slirp_pollfds_count;
  unsigned int tme_slirp_pollfds_size;
  struct event_set_return *tme_slirp_esrs;

  /* slirp's timers: */
  struct tme_slirp_timer *tme_slirp_timers;
};

/* this returns the time, in milliseconds, on a clock that doesn't
   jump: */
static tme_int64_t
_tme_slirp_clock_ms(void)
{
#if defined(CLOCK_MONOTONIC) && !defined(WIN32)
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return (((tme_int64_t) ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
  }
#endif
  {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (((tme_int64_t) tv.tv_sec * 1000) + (tv.tv_usec / 1000));
  }
}

/* this wakes up the reader: */
static void
_tme_slirp_wake(struct tme_slirp *sl)
{
  char byte;

  /* if the pipe is full, the reader is already awake: */
  byte = 0;
  if (write(sl->tme_slirp_wakeup[1], &byte, 1) < 0) {
  }
}

/* slirp calls this to send a frame to the machine: */
static ssize_t
_tme_slirp_cb_send_packet(const void *buf, size_t len, void *opaque)
{
  struct tme_slirp *sl;
  struct tme_slirp_frame *frame;

  sl = (struct tme_slirp *) opaque;
  if (len > TME_ETHERNET_FRAME_MAX) {
    tme_log(&sl->tme_slirp_element->tme_element_log_handle, 0, TME_OK,
	    (&sl->tme_slirp_element->tme_element_log_handle,
	     _("dropped a %ld byte frame from slirp"), (long) len));
    return (len);
  }
  if (sl->tme_slirp_queue_count == TME_SLIRP_QUEUE_MAX) {
    if (!sl->tme_slirp_queue_warned) {
      sl->tme_slirp_queue_warned = TRUE;
      tme_log(&sl->tme_slirp_element->tme_element_log_handle, 0, TME_OK,
	      (&sl->tme_slirp_element->tme_element_log_handle,
	       _("the machine isn't keeping up; dropping frames")));
    }
    return (len);
  }
  frame = &sl->tme_slirp_queue[(sl->tme_slirp_queue_head
				+ sl->tme_slirp_queue_count)
			       % TME_SLIRP_QUEUE_MAX];
  memcpy(frame->tme_slirp_frame_bytes, buf, len);
  frame->tme_slirp_frame_len = len;
  sl->tme_slirp_queue_count++;
  _tme_slirp_wake(sl);
  return (len);
}

/* slirp calls this when the machine does something wrong: */
static void
_tme_slirp_cb_guest_error(const char *msg, void *opaque)
{
  struct tme_slirp *sl;

  sl = (struct tme_slirp *) opaque;
  tme_log(&sl->tme_slirp_element->tme_element_log_handle, 0, TME_OK,
	  (&sl->tme_slirp_element->tme_element_log_handle,
	   _("slirp: %s"), msg));
}

/* slirp calls this for the time, in nanoseconds: */
static int64_t
_tme_slirp_cb_clock_get_ns(void *opaque)
{
  return (_tme_slirp_clock_ms() * 1000000);
}

/* slirp calls this to make a timer: */
static void *
_tme_slirp_cb_timer_new(SlirpTimerCb cb, void *cb_opaque, void *opaque)
{
  struct tme_slirp *sl;
  struct tme_slirp_timer *timer;

  sl = (struct tme_slirp *) opaque;
  timer = tme_new0(struct tme_slirp_timer, 1);
  timer->tme_slirp_timer_cb = cb;
  timer->tme_slirp_timer_opaque = cb_opaque;
  timer->tme_slirp_timer_next = sl->tme_slirp_timers;
  sl->tme_slirp_timers = timer;
  return (timer);
}

/* slirp calls this to free a timer: */
static void
_tme_slirp_cb_timer_free(void *_timer, void *opaque)
{
  struct tme_slirp *sl;
  struct tme_slirp_timer **_prev;

  sl = (struct tme_slirp *) opaque;
  for (_prev = &sl->tme_slirp_timers;
       *_prev != NULL;
       _prev = &(*_prev)->tme_slirp_timer_next) {
    if (*_prev == (struct tme_slirp_timer *) _timer) {
      *_prev = (*_prev)->tme_slirp_timer_next;
      tme_free(_timer);
      return;
    }
  }
}

/* slirp calls this to set a timer to expire at a time in
   milliseconds: */
static void
_tme_slirp_cb_timer_mod(void *_timer, int64_t expire_time, void *opaque)
{
  struct tme_slirp *sl;
  struct tme_slirp_timer *timer;

  sl = (struct tme_slirp *) opaque;
  timer = (struct tme_slirp_timer *) _timer;
  timer->tme_slirp_timer_armed = TRUE;
  timer->tme_slirp_timer_expire = expire_time;
  _tme_slirp_wake(sl);
}

/* slirp calls these about descriptors it will poll, which we don't
   need to know about in advance: */
static void
_tme_slirp_cb_register_poll_socket(slirp_os_socket fd, void *opaque)
{
}
static void
_tme_slirp_cb_unregister_poll_socket(slirp_os_socket fd, void *opaque)
{
}

/* slirp calls this when it may have more to do: */
static void
_tme_slirp_cb_notify(void *opaque)
{
  _tme_slirp_wake((struct tme_slirp *) opaque);
}

/* the slirp callbacks: */
static const SlirpCb _tme_slirp_cbs = {
  .send_packet = _tme_slirp_cb_send_packet,
  .guest_error = _tme_slirp_cb_guest_error,
  .clock_get_ns = _tme_slirp_cb_clock_get_ns,
  .timer_new = _tme_slirp_cb_timer_new,
  .timer_free = _tme_slirp_cb_timer_free,
  .timer_mod = _tme_slirp_cb_timer_mod,
  .notify = _tme_slirp_cb_notify,
  .register_poll_socket = _tme_slirp_cb_register_poll_socket,
  .unregister_poll_socket = _tme_slirp_cb_unregister_poll_socket,
};

/* slirp calls this to add a descriptor to poll, and gets back its
   index: */
static int
_tme_slirp_cb_add_poll(slirp_os_socket fd, int events, void *opaque)
{
  struct tme_slirp *sl;
  struct tme_slirp_pollfd *pollfd;
  unsigned int pollfd_i;

  sl = (struct tme_slirp *) opaque;

  /* an event set can only have a descriptor once: */
  for (pollfd_i = 0; pollfd_i < sl->tme_slirp_pollfds_count; pollfd_i++) {
    pollfd = &sl->tme_slirp_pollfds[pollfd_i];
    if (pollfd->tme_slirp_pollfd_fd == fd) {
      pollfd->tme_slirp_pollfd_events |= events;
      return (pollfd_i);
    }
  }

  if (sl->tme_slirp_pollfds_count == sl->tme_slirp_pollfds_size) {
    sl->tme_slirp_pollfds_size *= 2;
    sl->tme_slirp_pollfds = tme_renew(struct tme_slirp_pollfd,
				      sl->tme_slirp_pollfds,
				      sl->tme_slirp_pollfds_size);
    sl->tme_slirp_esrs = tme_renew(struct event_set_return,
				   sl->tme_slirp_esrs,
				   sl->tme_slirp_pollfds_size);
  }
  pollfd = &sl->tme_slirp_pollfds[sl->tme_slirp_pollfds_count];
  pollfd->tme_slirp_pollfd_fd = fd;
  pollfd->tme_slirp_pollfd_events = events;
  pollfd->tme_slirp_pollfd_revents = 0;
  return (sl->tme_slirp_pollfds_count++);
}

/* slirp calls this to get the events on a descriptor it added: */
static int
_tme_slirp_cb_get_revents(int idx, void *opaque)
{
  struct tme_slirp *sl;

  sl = (struct tme_slirp *) opaque;
  if (idx < 0 || (unsigned int) idx >= sl->tme_slirp_pollfds_count) {
    return (0);
  }
  return (sl->tme_slirp_pollfds[idx].tme_slirp_pollfd_revents);
}

/* this runs the expired timers, and returns the milliseconds until
   the next one expires, at most the given limit: */
static tme_uint32_t
_tme_slirp_timers_run(struct tme_slirp *sl, tme_uint32_t limit)
{
  struct tme_slirp_timer *timer;
  tme_int64_t now;

  /* a timer callback can free and make timers, so start over after
     each one: */
  for (;;) {
    now = _tme_slirp_clock_ms();
    for (timer = sl->tme_slirp_timers;
	 timer != NULL;
	 timer = timer->tme_slirp_timer_next) {
      if (timer->tme_slirp_timer_armed
	  && timer->tme_slirp_timer_expire <= now) {
	break;
      }
    }
    if (timer == NULL) {
      break;
    }
    timer->tme_slirp_timer_armed = FALSE;
    (*timer->tme_slirp_timer_cb)(timer->tme_slirp_timer_opaque);
  }

  for (timer = sl->tme_slirp_timers;
       timer != NULL;
       timer = timer->tme_slirp_timer_next) {
    if (timer->tme_slirp_timer_armed
	&& timer->tme_slirp_timer_expire - now < (tme_int64_t) limit) {
      limit = timer->tme_slirp_timer_expire - now;
    }
  }
  return (limit);
}

/* this is called with the Ethernet mutex locked to send a frame: */
static int
_tme_slirp_write(void *data)
{
  struct tme_slirp *sl;
  size_t len;

  sl = (struct tme_slirp *) data;
  len = sl->tme_slirp_eth->tme_eth_data_length;
  slirp_input(sl->tme_slirp_slirp, sl->tme_slirp_buffer_out, len);

  /* the reader must poll slirp's sockets again, since this frame may
     have made or closed some: */
  sl->tme_slirp_input_count++;
  _tme_slirp_wake(sl);

  /* the frame was written: */
  return (len);
}

/* this gets the descriptors that slirp wants polled, and returns
   how long slirp can wait, in milliseconds.  the wakeup pipe goes
   last: */
static tme_uint32_t
_tme_slirp_fill(struct tme_slirp *sl)
{
  tme_uint32_t timeout_ms;

  sl->tme_slirp_pollfds_count = 0;
  timeout_ms = _tme_slirp_timers_run(sl, TME_SLIRP_WAIT_MAX);
  slirp_pollfds_fill_socket(sl->tme_slirp_slirp, &timeout_ms,
			    _tme_slirp_cb_add_poll, sl);
  _tme_slirp_cb_add_poll(sl->tme_slirp_wakeup[0], SLIRP_POLL_IN, sl);
  return (timeout_ms);
}

/* this waits for the descriptors from _tme_slirp_fill.  it unlocks
   the mutex while waiting.  with cooperative threads, a wait that
   blocks never returns: the reader starts over: */
static int
_tme_slirp_wait(struct tme_slirp *sl, tme_uint32_t timeout_ms)
{
  struct tme_slirp_pollfd *pollfd;
  tme_event_set_t *events;
  int events_max;
  struct timeval timeout;
  unsigned int pollfd_i;
  int events_slirp;
  int rc;

  events_max = sl->tme_slirp_pollfds_count;
  events = tme_event_set_init(&events_max, EVENT_METHOD_FAST);
  for (pollfd_i = 0; pollfd_i < sl->tme_slirp_pollfds_count; pollfd_i++) {
    pollfd = &sl->tme_slirp_pollfds[pollfd_i];
    events_slirp = pollfd->tme_slirp_pollfd_events;
    tme_event_ctl(events, pollfd->tme_slirp_pollfd_fd,
		  (((events_slirp & (SLIRP_POLL_IN | SLIRP_POLL_PRI))
		    ? EVENT_READ
		    : 0)
		   | ((events_slirp & SLIRP_POLL_OUT)
		      ? EVENT_WRITE
		      : 0)),
		  (void *) (unsigned long) pollfd_i);
  }
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  rc = tme_event_wait(events, &timeout,
		      sl->tme_slirp_esrs, sl->tme_slirp_pollfds_count,
		      &sl->tme_slirp_eth->tme_eth_mutex);
  tme_event_free(events);
  return (rc);
}

/* this polls slirp's descriptors without waiting, and lets slirp
   handle the ones that are ready, and its timers: */
static void
_tme_slirp_poll(struct tme_slirp *sl)
{
  struct tme_slirp_pollfd *pollfd;
  unsigned int input_count;
  unsigned int pollfd_i;
  int events_slirp;
  int revents;
  int esr_i;
  int rc;

  _tme_slirp_fill(sl);
  for (pollfd_i = 0; pollfd_i < sl->tme_slirp_pollfds_count; pollfd_i++) {
    sl->tme_slirp_pollfds[pollfd_i].tme_slirp_pollfd_revents = 0;
  }
  input_count = sl->tme_slirp_input_count;
  rc = _tme_slirp_wait(sl, 0);

  /* if a frame from the machine went to slirp while the mutex was
     unlocked, these events may be for sockets that slirp has since
     closed or replaced, so slirp only runs its timers this time: */
  if (input_count != sl->tme_slirp_input_count) {
    rc = 0;
  }

  /* give slirp the events.  an error or hangup is reported as
     readable.  slirp reads a descriptor it wanted to read from, and
     one that it only wanted to write to (a connect that failed) is
     given a hangup.  the wakeup pipe, last, isn't slirp's: */
  for (esr_i = 0; esr_i < rc; esr_i++) {
    pollfd_i = (unsigned long) sl->tme_slirp_esrs[esr_i].arg;
    if (pollfd_i + 1 >= sl->tme_slirp_pollfds_count) {
      continue;
    }
    pollfd = &sl->tme_slirp_pollfds[pollfd_i];
    events_slirp = pollfd->tme_slirp_pollfd_events;
    revents = 0;
    if (sl->tme_slirp_esrs[esr_i].rwflags & EVENT_READ) {
      revents |= ((events_slirp & SLIRP_POLL_IN)
		  ? SLIRP_POLL_IN
		  : SLIRP_POLL_HUP);
    }
    if (sl->tme_slirp_esrs[esr_i].rwflags & EVENT_WRITE) {
      revents |= SLIRP_POLL_OUT;
    }
    pollfd->tme_slirp_pollfd_revents = revents;
  }
  slirp_pollfds_poll(sl->tme_slirp_slirp, (rc < 0),
		     _tme_slirp_cb_get_revents, sl);
  _tme_slirp_timers_run(sl, 0);
}

/* this is called with the Ethernet mutex locked to receive a frame.
   it returns the length of the frame in the Ethernet buffer: */
static int
_tme_slirp_read(void *data)
{
  struct tme_slirp *sl;
  struct tme_slirp_frame *frame;
  tme_uint32_t timeout_ms;
  char drain[64];
  size_t len;

  sl = (struct tme_slirp *) data;

  for (;;) {

    /* hand the machine the next queued frame: */
    if (sl->tme_slirp_queue_count > 0) {
      frame = &sl->tme_slirp_queue[sl->tme_slirp_queue_head];
      len = frame->tme_slirp_frame_len;
      memcpy(sl->tme_slirp_buffer_in, frame->tme_slirp_frame_bytes, len);
      sl->tme_slirp_queue_head = (sl->tme_slirp_queue_head + 1) % TME_SLIRP_QUEUE_MAX;
      sl->tme_slirp_queue_count--;
      if (sl->tme_slirp_queue_count == 0) {
	sl->tme_slirp_queue_warned = FALSE;
      }

      /* pad short frames to the Ethernet minimum: */
      if (len < TME_SLIRP_FRAME_MIN) {
	memset(sl->tme_slirp_buffer_in + len, 0, TME_SLIRP_FRAME_MIN - len);
	len = TME_SLIRP_FRAME_MIN;
      }
      return (len);
    }

    /* empty the wakeup pipe.  anything that wakes us after this
       leaves it readable: */
    while (read(sl->tme_slirp_wakeup[0], drain, sizeof(drain)) > 0) {
    }

    /* let slirp handle whatever is ready now: */
    _tme_slirp_poll(sl);
    if (sl->tme_slirp_queue_count > 0) {
      continue;
    }

    /* sleep until something is ready, or slirp has something to do.
       what is ready is found by the next _tme_slirp_poll, since with
       cooperative threads this wait doesn't return when it blocks: */
    timeout_ms = _tme_slirp_fill(sl);
    _tme_slirp_wait(sl, timeout_ms);
  }
}

/* this parses an IPv4 address: */
static int
_tme_slirp_arg_inet(const char *arg, struct in_addr *addr)
{
  return (arg != NULL
	  && inet_pton(AF_INET, arg, addr) == 1);
}

/* this parses a port number: */
static int
_tme_slirp_arg_port(const char *arg, int *_port)
{
  unsigned long port;
  char *end;

  if (arg == NULL
      || *arg < '0'
      || *arg > '9') {
    return (FALSE);
  }
  port = strtoul(arg, &end, 10);
  if (*end != '\0'
      || port == 0
      || port > 65535) {
    return (FALSE);
  }
  *_port = port;
  return (TRUE);
}

/* this parses a network, ADDRESS/BITS: */
static int
_tme_slirp_arg_net(const char *arg, struct in_addr *net, struct in_addr *mask)
{
  char buffer[INET_ADDRSTRLEN + 4];
  char *slash;
  unsigned long bits;
  char *end;

  if (arg == NULL
      || strlen(arg) >= sizeof(buffer)) {
    return (FALSE);
  }
  strcpy(buffer, arg);
  slash = strchr(buffer, '/');
  if (slash == NULL) {
    return (FALSE);
  }
  *(slash++) = '\0';
  bits = strtoul(slash, &end, 10);
  if (*slash == '\0'
      || *end != '\0'
      || bits < 1
      || bits > 30
      || !_tme_slirp_arg_inet(buffer, net)) {
    return (FALSE);
  }
  mask->s_addr = htonl(0xffffffffUL << (32 - bits));
  net->s_addr &= mask->s_addr;
  return (TRUE);
}

/* this parses a port forward, tcp|udp [HOSTADDR] HOSTPORT [GUESTADDR]
   GUESTPORT, after the "forward": */
static int
_tme_slirp_arg_forward(const char * const args[], int *_arg_i,
		       struct tme_slirp_forward *forward)
{
  int arg_i;

  arg_i = *_arg_i + 1;
  memset(forward, 0, sizeof(*forward));
  if (TME_ARG_IS(args[arg_i], "tcp")) {
    forward->tme_slirp_forward_udp = FALSE;
  }
  else if (TME_ARG_IS(args[arg_i], "udp")) {
    forward->tme_slirp_forward_udp = TRUE;
  }
  else {
    return (FALSE);
  }
  arg_i++;

  /* the host address defaults to the loopback address, so that only
     this host can reach the machine: */
  forward->tme_slirp_forward_host_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (_tme_slirp_arg_inet(args[arg_i], &forward->tme_slirp_forward_host_addr)) {
    arg_i++;
  }
  if (!_tme_slirp_arg_port(args[arg_i], &forward->tme_slirp_forward_host_port)) {
    return (FALSE);
  }
  arg_i++;

  /* the guest address defaults to the first DHCP address: */
  if (_tme_slirp_arg_inet(args[arg_i], &forward->tme_slirp_forward_guest_addr)) {
    arg_i++;
  }
  if (!_tme_slirp_arg_port(args[arg_i], &forward->tme_slirp_forward_guest_port)) {
    return (FALSE);
  }
  arg_i++;

  *_arg_i = arg_i;
  return (TRUE);
}

/* this returns nonzero if an address is a host address on a
   network: */
static int
_tme_slirp_on_net(struct in_addr addr, struct in_addr net, struct in_addr mask)
{
  return ((addr.s_addr & mask.s_addr) == net.s_addr
	  && (addr.s_addr & ~mask.s_addr) != 0
	  && (addr.s_addr & ~mask.s_addr) != ~mask.s_addr);
}

/* the new slirp Ethernet function: */
TME_ELEMENT_SUB_NEW_DECL(tme_host_slirp,eth) {
  struct tme_slirp *sl;
  SlirpConfig cfg;
  struct tme_slirp_forward forwards[TME_SLIRP_FORWARDS_MAX];
  struct tme_slirp_forward *forward;
  unsigned int forwards_count;
  unsigned int forward_i;
  int host_given, dns_given, dhcp_given;
  const char *tftp_path;
  const char *bootfile;
  const char *hostname;
  char addr_buf[4][INET_ADDRSTRLEN];
  struct in_addr guest_addr;
  int arg_i;
  int usage;
  int saved_errno;
  int rc;

  /* the defaults are QEMU's: */
  memset(&cfg, 0, sizeof(cfg));
  cfg.version = 6;
  cfg.in_enabled = TRUE;
  cfg.vnetwork.s_addr = htonl(0x0a000200);	/* 10.0.2.0 */
  cfg.vnetmask.s_addr = htonl(0xffffff00);	/* 255.255.255.0 */
  host_given = dns_given = dhcp_given = FALSE;
  tftp_path = NULL;
  bootfile = NULL;
  hostname = NULL;
  forwards_count = 0;

  /* get the arguments: */
  usage = FALSE;
  for (arg_i = 1;;) {

    /* the network: */
    if (TME_ARG_IS(args[arg_i + 0], "net")
	&& _tme_slirp_arg_net(args[arg_i + 1], &cfg.vnetwork, &cfg.vnetmask)) {
      arg_i += 2;
    }

    /* slirp's own address, the gateway: */
    else if (TME_ARG_IS(args[arg_i + 0], "host")
	     && _tme_slirp_arg_inet(args[arg_i + 1], &cfg.vhost)) {
      host_given = TRUE;
      arg_i += 2;
    }

    /* the DNS forwarder's address: */
    else if (TME_ARG_IS(args[arg_i + 0], "dns")
	     && _tme_slirp_arg_inet(args[arg_i + 1], &cfg.vnameserver)) {
      dns_given = TRUE;
      arg_i += 2;
    }

    /* the first address that DHCP hands out: */
    else if (TME_ARG_IS(args[arg_i + 0], "dhcp")
	     && _tme_slirp_arg_inet(args[arg_i + 1], &cfg.vdhcp_start)) {
      dhcp_given = TRUE;
      arg_i += 2;
    }

    /* the hostname that DHCP hands out: */
    else if (TME_ARG_IS(args[arg_i + 0], "hostname")
	     && args[arg_i + 1] != NULL) {
      hostname = args[arg_i + 1];
      arg_i += 2;
    }

    /* the directory that the TFTP server serves: */
    else if (TME_ARG_IS(args[arg_i + 0], "tftp")
	     && args[arg_i + 1] != NULL) {
      tftp_path = args[arg_i + 1];
      arg_i += 2;
    }

    /* the boot file that DHCP (BOOTP) names: */
    else if (TME_ARG_IS(args[arg_i + 0], "bootfile")
	     && args[arg_i + 1] != NULL) {
      bootfile = args[arg_i + 1];
      arg_i += 2;
    }

    /* no connections out of the network, except forwards: */
    else if (TME_ARG_IS(args[arg_i + 0], "restrict")) {
      cfg.restricted = TRUE;
      arg_i++;
    }

    /* a port forward from the host to the machine: */
    else if (TME_ARG_IS(args[arg_i + 0], "forward")
	     && forwards_count < TME_SLIRP_FORWARDS_MAX
	     && _tme_slirp_arg_forward(args, &arg_i, &forwards[forwards_count])) {
      forwards_count++;
    }

    /* if we ran out of arguments: */
    else if (args[arg_i + 0] == NULL) {
      break;
    }

    /* otherwise this is a bad argument: */
    else {
      tme_output_append_error(_output,
			      "%s %s",
			      args[arg_i],
			      _("unexpected"));
      usage = TRUE;
      break;
    }
  }

  /* the addresses on the network default to QEMU's: */
  if (!host_given) {
    cfg.vhost.s_addr = cfg.vnetwork.s_addr | htonl(2);
  }
  if (!dns_given) {
    cfg.vnameserver.s_addr = cfg.vnetwork.s_addr | htonl(3);
  }
  if (!dhcp_given) {
    cfg.vdhcp_start.s_addr = cfg.vnetwork.s_addr | htonl(15);
  }
  if (!usage
      && (!_tme_slirp_on_net(cfg.vhost, cfg.vnetwork, cfg.vnetmask)
	  || !_tme_slirp_on_net(cfg.vnameserver, cfg.vnetwork, cfg.vnetmask)
	  || !_tme_slirp_on_net(cfg.vdhcp_start, cfg.vnetwork, cfg.vnetmask)
	  || cfg.vhost.s_addr == cfg.vnameserver.s_addr
	  || cfg.vhost.s_addr == cfg.vdhcp_start.s_addr
	  || cfg.vnameserver.s_addr == cfg.vdhcp_start.s_addr)) {
    tme_output_append_error(_output,
			    _("the host, dns and dhcp addresses must be different addresses on the network"));
    usage = TRUE;
  }

  if (usage) {
    tme_output_append_error(_output,
			    "%s %s [ net %s ] [ host %s ] [ dns %s ] [ dhcp %s ] [ hostname %s ] [ tftp %s ] [ bootfile %s ] [ restrict ] [ forward tcp|udp [ %s ] %s [ %s ] %s ]...",
			    _("usage:"),
			    args[0],
			    _("ADDRESS/BITS"),
			    _("ADDRESS"),
			    _("ADDRESS"),
			    _("ADDRESS"),
			    _("NAME"),
			    _("DIRECTORY"),
			    _("FILE"),
			    _("HOST-ADDRESS"),
			    _("HOST-PORT"),
			    _("ADDRESS"),
			    _("PORT"));
    return (EINVAL);
  }
  cfg.vhostname = hostname;
  cfg.tftp_path = tftp_path;
  cfg.bootfile = bootfile;

  /* start our data structure: */
  sl = tme_new0(struct tme_slirp, 1);
  sl->tme_slirp_element = element;
  sl->tme_slirp_queue = tme_new(struct tme_slirp_frame, TME_SLIRP_QUEUE_MAX);
  sl->tme_slirp_pollfds_size = 16;
  sl->tme_slirp_pollfds = tme_new(struct tme_slirp_pollfd, sl->tme_slirp_pollfds_size);
  sl->tme_slirp_esrs = tme_new(struct event_set_return, sl->tme_slirp_pollfds_size);

  /* make the wakeup pipe: */
  if (pipe(sl->tme_slirp_wakeup) < 0
      || fcntl(sl->tme_slirp_wakeup[0], F_SETFL, O_NONBLOCK) < 0
      || fcntl(sl->tme_slirp_wakeup[1], F_SETFL, O_NONBLOCK) < 0) {
    saved_errno = errno;
    tme_output_append_error(_output, _("pipe"));
    tme_free(sl->tme_slirp_queue);
    tme_free(sl->tme_slirp_pollfds);
    tme_free(sl->tme_slirp_esrs);
    tme_free(sl);
    return (saved_errno);
  }

  /* start slirp: */
  sl->tme_slirp_slirp = slirp_new(&cfg, &_tme_slirp_cbs, sl);
  if (sl->tme_slirp_slirp == NULL) {
    tme_output_append_error(_output, _("slirp could not start"));
    rc = ENOMEM;
    goto fail;
  }

  /* add the port forwards: */
  for (forward_i = 0; forward_i < forwards_count; forward_i++) {
    forward = &forwards[forward_i];
    guest_addr = forward->tme_slirp_forward_guest_addr;
    if (guest_addr.s_addr == 0) {
      guest_addr = cfg.vdhcp_start;
    }
    inet_ntop(AF_INET, &forward->tme_slirp_forward_host_addr, addr_buf[0], sizeof(addr_buf[0]));
    inet_ntop(AF_INET, &guest_addr, addr_buf[1], sizeof(addr_buf[1]));
    errno = 0;
    if (slirp_add_hostfwd(sl->tme_slirp_slirp,
			  forward->tme_slirp_forward_udp,
			  forward->tme_slirp_forward_host_addr,
			  forward->tme_slirp_forward_host_port,
			  guest_addr,
			  forward->tme_slirp_forward_guest_port) < 0) {
      rc = (errno != 0 ? errno : EADDRINUSE);
      tme_output_append_error(_output, _("forward %s %s %d"),
			      (forward->tme_slirp_forward_udp ? "udp" : "tcp"),
			      addr_buf[0],
			      forward->tme_slirp_forward_host_port);
      goto fail;
    }
    tme_log(&element->tme_element_log_handle, 0, TME_OK,
	    (&element->tme_element_log_handle,
	     _("forwarding %s %s:%d to %s:%d"),
	     (forward->tme_slirp_forward_udp ? "udp" : "tcp"),
	     addr_buf[0],
	     forward->tme_slirp_forward_host_port,
	     addr_buf[1],
	     forward->tme_slirp_forward_guest_port));
  }

  inet_ntop(AF_INET, &cfg.vnetwork, addr_buf[0], sizeof(addr_buf[0]));
  inet_ntop(AF_INET, &cfg.vhost, addr_buf[1], sizeof(addr_buf[1]));
  inet_ntop(AF_INET, &cfg.vnameserver, addr_buf[2], sizeof(addr_buf[2]));
  inet_ntop(AF_INET, &cfg.vdhcp_start, addr_buf[3], sizeof(addr_buf[3]));
  tme_log(&element->tme_element_log_handle, 0, TME_OK,
	  (&element->tme_element_log_handle,
	   _("slirp %s on %s, gateway %s, DNS %s, DHCP from %s%s"),
	   slirp_version_string(),
	   addr_buf[0],
	   addr_buf[1],
	   addr_buf[2],
	   addr_buf[3],
	   (cfg.restricted ? _(", restricted") : "")));

  /* start the Ethernet: */
  rc = tme_eth_init(element,
		    TME_INVALID_HANDLE,
		    TME_ETHERNET_FRAME_MAX,
		    sl,
		    NULL);
  if (rc != TME_OK) {
    goto fail;
  }

  sl->tme_slirp_eth = (struct tme_ethernet *) element->tme_element_private;
  sl->tme_slirp_eth->tme_eth_buffer = sl->tme_slirp_buffer_in;
  sl->tme_slirp_eth->tme_eth_out = sl->tme_slirp_buffer_out;
  sl->tme_slirp_eth->tme_ethernet_read = _tme_slirp_read;
  sl->tme_slirp_eth->tme_ethernet_write = _tme_slirp_write;
  tme_eth_start(sl->tme_slirp_eth);

  return (TME_OK);

 fail:
  if (sl->tme_slirp_slirp != NULL) {
    slirp_cleanup(sl->tme_slirp_slirp);
  }
  close(sl->tme_slirp_wakeup[0]);
  close(sl->tme_slirp_wakeup[1]);
  tme_free(sl->tme_slirp_queue);
  tme_free(sl->tme_slirp_pollfds);
  tme_free(sl->tme_slirp_esrs);
  tme_free(sl);
  return (rc);
}
