/* host/eth/udp/udp-eth.c - Ethernet over UDP support: */

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

/* this carries Ethernet frames over UDP, one raw frame per datagram
   with no header, as Johnny Billquist's HECnet bridge, simh (attach
   xq udp:...), QEMU (-netdev dgram) and gobootd's bootbridge do.

   frames go to fixed peers given with "peer", and to learned peers:
   any address that sends us a frame becomes a peer until it has been
   quiet for TME_UDP_PEER_TIMEOUT seconds.  unicast frames go only to
   the peer that last sent a frame from that destination address;
   broadcast, multicast and unknown destinations go to every peer: */

#include <tme/common.h>

/* includes: */
#include "eth-if.h"
#include <time.h>

/* macros: */

/* the most fixed and learned peers: */
#define TME_UDP_PEERS_MAX	(32)

/* the most learned Ethernet addresses: */
#define TME_UDP_ADDRS_MAX	(256)

/* seconds after which a quiet learned peer is forgotten.  this is
   the HECnet bridge's timeout for passive peers: */
#define TME_UDP_PEER_TIMEOUT	(180)

/* the smallest frame we send or pass on, without the CRC: */
#define TME_UDP_FRAME_MIN	(TME_ETHERNET_FRAME_MIN - TME_ETHERNET_CRC_SIZE)

/* structures: */

/* a peer: */
struct tme_udp_peer {

  /* nonzero if this slot is in use: */
  int tme_udp_peer_used;

  /* nonzero if this peer was given on the command line: */
  int tme_udp_peer_fixed;

  /* this is incremented whenever the slot is reused, so that stale
     learned addresses can't point at a new peer: */
  unsigned int tme_udp_peer_generation;

  /* the last time this peer sent us a frame: */
  time_t tme_udp_peer_seen;

  /* the peer's address: */
  struct sockaddr_storage tme_udp_peer_addr;
  socklen_t tme_udp_peer_addr_len;
};

/* a learned Ethernet address: */
struct tme_udp_addr {

  /* nonzero if this slot is in use: */
  int tme_udp_addr_used;

  /* the Ethernet address: */
  tme_uint8_t tme_udp_addr_addr[TME_ETHERNET_ADDR_SIZE];

  /* the peer that last sent a frame from this address, and its
     generation at that time: */
  unsigned int tme_udp_addr_peer;
  unsigned int tme_udp_addr_generation;

  /* the last time a frame came from this address: */
  time_t tme_udp_addr_seen;
};

/* an address argument: */
struct tme_udp_addr_arg {
  char *tme_udp_addr_arg_host;
  char *tme_udp_addr_arg_port;
};

/* our private data: */
struct tme_udp {

  /* backpointer to our element: */
  struct tme_element *tme_udp_element;

  /* our Ethernet: */
  struct tme_ethernet *tme_udp_eth;

  /* our socket, and its address family: */
  int tme_udp_fd;
  int tme_udp_family;

  /* the frame buffers.  the receive buffer has one extra byte, to
     detect oversized datagrams: */
  tme_uint8_t tme_udp_buffer_in[TME_ETHERNET_FRAME_MAX + 1];
  tme_uint8_t tme_udp_buffer_out[TME_ETHERNET_FRAME_MAX];

  /* the peers: */
  struct tme_udp_peer tme_udp_peers[TME_UDP_PEERS_MAX];

  /* the learned Ethernet addresses: */
  struct tme_udp_addr tme_udp_addrs[TME_UDP_ADDRS_MAX];
};

/* this returns nonzero if two socket addresses are the same: */
static int
_tme_udp_sockaddr_equal(const struct sockaddr_storage *a, socklen_t a_len,
			const struct sockaddr_storage *b, socklen_t b_len)
{
  const struct sockaddr_in *a4, *b4;
  const struct sockaddr_in6 *a6, *b6;

  if (a->ss_family != b->ss_family) {
    return (FALSE);
  }
  switch (a->ss_family) {
  case AF_INET:
    a4 = (const struct sockaddr_in *) a;
    b4 = (const struct sockaddr_in *) b;
    return (a4->sin_port == b4->sin_port
	    && a4->sin_addr.s_addr == b4->sin_addr.s_addr);
  case AF_INET6:
    a6 = (const struct sockaddr_in6 *) a;
    b6 = (const struct sockaddr_in6 *) b;
    return (a6->sin6_port == b6->sin6_port
	    && !memcmp(&a6->sin6_addr, &b6->sin6_addr, sizeof(a6->sin6_addr)));
  default:
    return (a_len == b_len && !memcmp(a, b, a_len));
  }
}

/* this formats a socket address for logging: */
static const char *
_tme_udp_sockaddr_string(const struct sockaddr_storage *ss, socklen_t len, char *buf, size_t size)
{
  char host[NI_MAXHOST];
  char serv[NI_MAXSERV];

  if (getnameinfo((const struct sockaddr *) ss, len,
		  host, sizeof(host), serv, sizeof(serv),
		  NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    snprintf(buf, size, "?");
  }
  else if (ss->ss_family == AF_INET6) {
    snprintf(buf, size, "[%s]:%s", host, serv);
  }
  else {
    snprintf(buf, size, "%s:%s", host, serv);
  }
  return (buf);
}

/* this returns nonzero if a string is a decimal port number: */
static int
_tme_udp_is_port(const char *string)
{
  if (string == NULL || *string == '\0') {
    return (FALSE);
  }
  for (; *string != '\0'; string++) {
    if (*string < '0' || *string > '9') {
      return (FALSE);
    }
  }
  return (TRUE);
}

/* this parses an address argument, either HOST PORT as two
   arguments, or HOST:PORT or [HOST]:PORT as one.  tmesh only allows
   colons and brackets in quoted arguments: */
static int
_tme_udp_arg_addr(const char * const args[], int *_arg_i,
		  struct tme_udp_addr_arg *addr_arg)
{
  const char *arg;
  char *host;
  char *port;
  char *end;

  arg = args[*_arg_i + 1];
  if (arg == NULL) {
    return (EINVAL);
  }

  /* HOST PORT: */
  if (_tme_udp_is_port(args[*_arg_i + 2])) {
    addr_arg->tme_udp_addr_arg_host = tme_strdup(arg);
    addr_arg->tme_udp_addr_arg_port = tme_strdup(args[*_arg_i + 2]);
    *_arg_i += 3;
    return (TME_OK);
  }

  /* HOST:PORT or [HOST]:PORT: */
  host = tme_strdup(arg);
  port = strrchr(host, ':');
  if (port == NULL || !_tme_udp_is_port(port + 1)) {
    tme_free(host);
    return (EINVAL);
  }
  *(port++) = '\0';
  if (host[0] == '[') {
    end = strchr(host, ']');
    if (end == NULL || end[1] != '\0') {
      tme_free(host);
      return (EINVAL);
    }
    *end = '\0';
    memmove(host, host + 1, strlen(host));
  }
  addr_arg->tme_udp_addr_arg_host = host;
  addr_arg->tme_udp_addr_arg_port = tme_strdup(port);
  *_arg_i += 2;
  return (TME_OK);
}

/* this frees address arguments: */
static void
_tme_udp_addr_args_free(struct tme_udp_addr_arg *addr_args, unsigned int count)
{
  for (; count-- > 0; addr_args++) {
    tme_free(addr_args->tme_udp_addr_arg_host);
    tme_free(addr_args->tme_udp_addr_arg_port);
  }
}

/* this resolves an address.  family is the address family to
   resolve for, or AF_UNSPEC: */
static int
_tme_udp_resolve(const struct tme_udp_addr_arg *addr_arg, int family, int passive,
		 struct sockaddr_storage *ss, socklen_t *_len, char **_output)
{
  struct addrinfo hints;
  struct addrinfo *res;
  int rc;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_NUMERICSERV;
  if (passive) {
    hints.ai_flags |= AI_PASSIVE;
  }
#ifdef AI_V4MAPPED
  if (family == AF_INET6) {
    hints.ai_flags |= AI_V4MAPPED;
  }
#endif

  rc = getaddrinfo(addr_arg->tme_udp_addr_arg_host,
		   addr_arg->tme_udp_addr_arg_port,
		   &hints, &res);
  if (rc != 0) {
    tme_output_append_error(_output, "%s %s: %s",
			    addr_arg->tme_udp_addr_arg_host,
			    addr_arg->tme_udp_addr_arg_port,
			    gai_strerror(rc));
    return (ENOENT);
  }
  memcpy(ss, res->ai_addr, res->ai_addrlen);
  *_len = res->ai_addrlen;
  freeaddrinfo(res);
  return (TME_OK);
}

/* this returns nonzero if a socket address is a loopback address: */
static int
_tme_udp_sockaddr_loopback(const struct sockaddr_storage *ss)
{
  const struct sockaddr_in6 *sin6;

  switch (ss->ss_family) {
  case AF_INET:
    return ((ntohl(((const struct sockaddr_in *) ss)->sin_addr.s_addr) >> 24) == 127);
  case AF_INET6:
    sin6 = (const struct sockaddr_in6 *) ss;
    return (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr)
	    || (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)
		&& sin6->sin6_addr.s6_addr[12] == 127));
  default:
    return (FALSE);
  }
}

/* this forgets learned peers that have been quiet too long: */
static void
_tme_udp_peers_expire(struct tme_udp *udp, time_t now)
{
  struct tme_udp_peer *peer;
  unsigned int peer_i;

  for (peer_i = 0; peer_i < TME_UDP_PEERS_MAX; peer_i++) {
    peer = &udp->tme_udp_peers[peer_i];
    if (peer->tme_udp_peer_used
	&& !peer->tme_udp_peer_fixed
	&& (now - peer->tme_udp_peer_seen) > TME_UDP_PEER_TIMEOUT) {
      peer->tme_udp_peer_used = FALSE;
      peer->tme_udp_peer_generation++;
    }
  }
}

/* this notes a frame from a peer, learning the peer if it's new.  it
   returns the peer's index, or -1 if there is no room for it: */
static int
_tme_udp_peer_learn(struct tme_udp *udp,
		    const struct sockaddr_storage *ss, socklen_t len,
		    time_t now)
{
  struct tme_udp_peer *peer;
  unsigned int peer_i;
  int peer_free;
  char addr_buf[INET6_ADDRSTRLEN + 16];

  peer_free = -1;
  for (peer_i = 0; peer_i < TME_UDP_PEERS_MAX; peer_i++) {
    peer = &udp->tme_udp_peers[peer_i];
    if (!peer->tme_udp_peer_used) {
      if (peer_free < 0) {
	peer_free = peer_i;
      }
      continue;
    }
    if (_tme_udp_sockaddr_equal(&peer->tme_udp_peer_addr,
				peer->tme_udp_peer_addr_len,
				ss, len)) {
      peer->tme_udp_peer_seen = now;
      return (peer_i);
    }
  }

  if (peer_free < 0) {
    return (-1);
  }
  peer = &udp->tme_udp_peers[peer_free];
  peer->tme_udp_peer_used = TRUE;
  peer->tme_udp_peer_fixed = FALSE;
  peer->tme_udp_peer_seen = now;
  memcpy(&peer->tme_udp_peer_addr, ss, len);
  peer->tme_udp_peer_addr_len = len;
  tme_log(&udp->tme_udp_element->tme_element_log_handle, 0, TME_OK,
	  (&udp->tme_udp_element->tme_element_log_handle,
	   _("learned peer %s"),
	   _tme_udp_sockaddr_string(ss, len, addr_buf, sizeof(addr_buf))));
  return (peer_free);
}

/* this notes that frames from an Ethernet address came from a peer: */
static void
_tme_udp_addr_learn(struct tme_udp *udp, const tme_uint8_t *addr,
		    int peer_i, time_t now)
{
  struct tme_udp_addr *entry;
  struct tme_udp_addr *entry_use;
  unsigned int entry_i;

  /* only unicast addresses can be destinations: */
  if (addr[0] & 0x01) {
    return;
  }

  /* find this address, or else a free entry, or else the oldest entry: */
  entry_use = NULL;
  for (entry_i = 0; entry_i < TME_UDP_ADDRS_MAX; entry_i++) {
    entry = &udp->tme_udp_addrs[entry_i];
    if (entry->tme_udp_addr_used
	&& !memcmp(entry->tme_udp_addr_addr, addr, TME_ETHERNET_ADDR_SIZE)) {
      entry_use = entry;
      break;
    }
    if (entry_use == NULL
	|| (entry_use->tme_udp_addr_used
	    && (!entry->tme_udp_addr_used
		|| entry->tme_udp_addr_seen < entry_use->tme_udp_addr_seen))) {
      entry_use = entry;
    }
  }

  entry_use->tme_udp_addr_used = TRUE;
  memcpy(entry_use->tme_udp_addr_addr, addr, TME_ETHERNET_ADDR_SIZE);
  entry_use->tme_udp_addr_peer = peer_i;
  entry_use->tme_udp_addr_generation = udp->tme_udp_peers[peer_i].tme_udp_peer_generation;
  entry_use->tme_udp_addr_seen = now;
}

/* this returns the peer that an Ethernet address was learned from,
   or -1 if the frame should go to all peers: */
static int
_tme_udp_addr_lookup(struct tme_udp *udp, const tme_uint8_t *addr)
{
  struct tme_udp_addr *entry;
  struct tme_udp_peer *peer;
  unsigned int entry_i;

  if (addr[0] & 0x01) {
    return (-1);
  }
  for (entry_i = 0; entry_i < TME_UDP_ADDRS_MAX; entry_i++) {
    entry = &udp->tme_udp_addrs[entry_i];
    if (entry->tme_udp_addr_used
	&& !memcmp(entry->tme_udp_addr_addr, addr, TME_ETHERNET_ADDR_SIZE)) {
      peer = &udp->tme_udp_peers[entry->tme_udp_addr_peer];
      if (peer->tme_udp_peer_used
	  && peer->tme_udp_peer_generation == entry->tme_udp_addr_generation) {
	return (entry->tme_udp_addr_peer);
      }
      entry->tme_udp_addr_used = FALSE;
      return (-1);
    }
  }
  return (-1);
}

/* this sends a frame to one peer: */
static void
_tme_udp_send(struct tme_udp *udp, struct tme_udp_peer *peer, size_t len)
{
  char addr_buf[INET6_ADDRSTRLEN + 16];

  if (sendto(udp->tme_udp_fd, udp->tme_udp_buffer_out, len, 0,
	     (const struct sockaddr *) &peer->tme_udp_peer_addr,
	     peer->tme_udp_peer_addr_len) < 0) {

    /* like Ethernet, we don't guarantee delivery: */
    tme_log(&udp->tme_udp_element->tme_element_log_handle, 1, errno,
	    (&udp->tme_udp_element->tme_element_log_handle,
	     _("send to %s failed"),
	     _tme_udp_sockaddr_string(&peer->tme_udp_peer_addr,
				      peer->tme_udp_peer_addr_len,
				      addr_buf, sizeof(addr_buf))));
  }
}

/* this is called with the Ethernet mutex locked to send a frame: */
static int
_tme_udp_write(void *data)
{
  struct tme_udp *udp;
  const struct tme_ethernet_header *header;
  size_t len, len_send;
  unsigned int peer_i;
  int peer_dst;

  udp = (struct tme_udp *) data;
  len = udp->tme_udp_eth->tme_eth_data_length;
  header = (const struct tme_ethernet_header *) udp->tme_udp_buffer_out;

  /* pad short frames to the Ethernet minimum: */
  len_send = len;
  if (len_send < TME_UDP_FRAME_MIN) {
    memset(udp->tme_udp_buffer_out + len_send, 0, TME_UDP_FRAME_MIN - len_send);
    len_send = TME_UDP_FRAME_MIN;
  }

  _tme_udp_peers_expire(udp, time(NULL));

  /* send a unicast frame only to the peer its destination is behind,
     and anything else to every peer: */
  peer_dst = _tme_udp_addr_lookup(udp, header->tme_ethernet_header_dst);
  if (peer_dst >= 0) {
    _tme_udp_send(udp, &udp->tme_udp_peers[peer_dst], len_send);
  }
  else {
    for (peer_i = 0; peer_i < TME_UDP_PEERS_MAX; peer_i++) {
      if (udp->tme_udp_peers[peer_i].tme_udp_peer_used) {
	_tme_udp_send(udp, &udp->tme_udp_peers[peer_i], len_send);
      }
    }
  }

  /* the frame was written: */
  return (len);
}

/* this is called with the Ethernet mutex locked to receive a frame.
   it returns the length of the frame in the Ethernet buffer: */
static int
_tme_udp_read(void *data)
{
  struct tme_udp *udp;
  struct tme_ethernet *eth;
  tme_event_set_t *events;
  int events_max;
  struct event_set_return esr;
  struct sockaddr_storage from;
  socklen_t from_len;
  const struct tme_ethernet_header *header;
  ssize_t len;
  time_t now;
  int peer_i;
  int rc;

  udp = (struct tme_udp *) data;
  eth = udp->tme_udp_eth;

  for (;;) {

    /* wait until the socket is readable.  this unlocks the mutex
       while waiting.  with cooperative threads a wait that blocks
       never returns: the thread frees the event set and restarts
       later, so the event set can't be reused: */
    esr.rwflags = 0;
    events_max = 1;
    events = tme_event_set_init(&events_max, EVENT_METHOD_FAST);
    tme_event_ctl(events, udp->tme_udp_fd, EVENT_READ, 0);
    rc = tme_event_wait(events, NULL, &esr, 1, &eth->tme_eth_mutex);
    tme_event_free(events);
    if (rc < 0) {
      return (rc);
    }
    if (rc == 0 || !(esr.rwflags & EVENT_READ)) {
      continue;
    }

    /* the socket is nonblocking, in case another reader got the
       datagram first: */
    from_len = sizeof(from);
    len = recvfrom(udp->tme_udp_fd,
		   udp->tme_udp_buffer_in, sizeof(udp->tme_udp_buffer_in),
		   0, (struct sockaddr *) &from, &from_len);
    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
	  || errno == ECONNREFUSED) {
	continue;
      }
      return (-1);
    }

    /* drop datagrams that can't be Ethernet frames: */
    if (len < TME_ETHERNET_HEADER_SIZE
	|| len > TME_ETHERNET_FRAME_MAX) {
      tme_log(&udp->tme_udp_element->tme_element_log_handle, 1, TME_OK,
	      (&udp->tme_udp_element->tme_element_log_handle,
	       _("dropped a %ld byte datagram"), (long) len));
      continue;
    }

    /* learn the peer and the frame's source address: */
    now = time(NULL);
    _tme_udp_peers_expire(udp, now);
    peer_i = _tme_udp_peer_learn(udp, &from, from_len, now);
    header = (const struct tme_ethernet_header *) udp->tme_udp_buffer_in;
    if (peer_i >= 0) {
      _tme_udp_addr_learn(udp, header->tme_ethernet_header_src, peer_i, now);
    }

    /* pad short frames to the Ethernet minimum: */
    if (len < TME_UDP_FRAME_MIN) {
      memset(udp->tme_udp_buffer_in + len, 0, TME_UDP_FRAME_MIN - len);
      len = TME_UDP_FRAME_MIN;
    }

    return (len);
  }
}

/* this parses our arguments: */
static int
_tme_udp_args(const char * const args[],
	      struct tme_udp_addr_arg *listen_arg,
	      struct tme_udp_addr_arg *peer_args,
	      unsigned int *_peers_count,
	      char **_output)
{
  int arg_i;
  int usage;

  usage = FALSE;
  listen_arg->tme_udp_addr_arg_host = NULL;
  *_peers_count = 0;

  for (arg_i = 1;;) {

    /* the address to listen on: */
    if (TME_ARG_IS(args[arg_i + 0], "listen")
	&& listen_arg->tme_udp_addr_arg_host == NULL
	&& _tme_udp_arg_addr(args, &arg_i, listen_arg) == TME_OK) {
    }

    /* a peer that always gets frames: */
    else if (TME_ARG_IS(args[arg_i + 0], "peer")
	     && *_peers_count < TME_UDP_PEERS_MAX
	     && _tme_udp_arg_addr(args, &arg_i, &peer_args[*_peers_count]) == TME_OK) {
      (*_peers_count)++;
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

  if (listen_arg->tme_udp_addr_arg_host == NULL && *_peers_count == 0) {
    usage = TRUE;
  }

  if (usage) {
    if (listen_arg->tme_udp_addr_arg_host != NULL) {
      _tme_udp_addr_args_free(listen_arg, 1);
    }
    _tme_udp_addr_args_free(peer_args, *_peers_count);
    tme_output_append_error(_output,
			    "%s %s [ listen %s %s ] [ peer %s %s ]...",
			    _("usage:"),
			    args[0],
			    _("ADDRESS"),
			    _("PORT"),
			    _("ADDRESS"),
			    _("PORT"));
    return (EINVAL);
  }
  return (TME_OK);
}

/* the new UDP Ethernet function: */
TME_ELEMENT_SUB_NEW_DECL(tme_host_udp,eth) {
  struct tme_udp *udp;
  struct tme_udp_addr_arg listen_arg;
  struct tme_udp_addr_arg peer_args[TME_UDP_PEERS_MAX];
  unsigned int peers_count;
  unsigned int peer_i;
  struct tme_udp_peer *peer;
  struct sockaddr_storage listen_addr;
  socklen_t listen_addr_len;
  char addr_buf[INET6_ADDRSTRLEN + 16];
  int fd;
  int opt;
  int saved_errno;
  int rc;

  /* get the arguments: */
  rc = _tme_udp_args(args, &listen_arg, peer_args, &peers_count, _output);
  if (rc != TME_OK) {
    return (rc);
  }

  /* resolve the listen address.  without one, we listen on an
     ephemeral port on the loopback address, which only works with
     fixed peers on this host: */
  if (listen_arg.tme_udp_addr_arg_host == NULL) {
    listen_arg.tme_udp_addr_arg_host = tme_strdup("127.0.0.1");
    listen_arg.tme_udp_addr_arg_port = tme_strdup("0");
  }
  udp = tme_new0(struct tme_udp, 1);
  udp->tme_udp_element = element;
  rc = _tme_udp_resolve(&listen_arg, AF_UNSPEC, TRUE,
			&listen_addr, &listen_addr_len, _output);
  udp->tme_udp_family = listen_addr.ss_family;

  /* resolve the fixed peers in the same address family: */
  for (peer_i = 0; rc == TME_OK && peer_i < peers_count; peer_i++) {
    peer = &udp->tme_udp_peers[peer_i];
    rc = _tme_udp_resolve(&peer_args[peer_i], udp->tme_udp_family, FALSE,
			  &peer->tme_udp_peer_addr, &peer->tme_udp_peer_addr_len,
			  _output);
    peer->tme_udp_peer_used = TRUE;
    peer->tme_udp_peer_fixed = TRUE;
  }

  _tme_udp_addr_args_free(&listen_arg, 1);
  _tme_udp_addr_args_free(peer_args, peers_count);
  if (rc != TME_OK) {
    tme_free(udp);
    return (rc);
  }

  /* open and bind the socket: */
  fd = socket(udp->tme_udp_family, SOCK_DGRAM, 0);
  if (fd < 0) {
    saved_errno = errno;
    tme_output_append_error(_output, _("socket: %s"), strerror(saved_errno));
    tme_free(udp);
    return (saved_errno);
  }
#ifdef IPV6_V6ONLY
  if (udp->tme_udp_family == AF_INET6) {
    opt = 0;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
  }
#endif
  if (bind(fd, (struct sockaddr *) &listen_addr, listen_addr_len) < 0
      || fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) {
    saved_errno = errno;
    tme_output_append_error(_output, _("%s: %s"),
			    _tme_udp_sockaddr_string(&listen_addr, listen_addr_len,
						     addr_buf, sizeof(addr_buf)),
			    strerror(saved_errno));
    close(fd);
    tme_free(udp);
    return (saved_errno);
  }
  udp->tme_udp_fd = fd;

  /* log the address we're on, and warn if anyone on the network can
     put frames on our segment: */
  listen_addr_len = sizeof(listen_addr);
  getsockname(fd, (struct sockaddr *) &listen_addr, &listen_addr_len);
  tme_log(&element->tme_element_log_handle, 0, TME_OK,
	  (&element->tme_element_log_handle,
	   _("exchanging frames on UDP %s"),
	   _tme_udp_sockaddr_string(&listen_addr, listen_addr_len,
				    addr_buf, sizeof(addr_buf))));
  if (!_tme_udp_sockaddr_loopback(&listen_addr)) {
    tme_log(&element->tme_element_log_handle, 0, TME_OK,
	    (&element->tme_element_log_handle,
	     _("warning: anyone who can reach %s can send and receive frames"),
	     addr_buf));
  }

  /* start the Ethernet: */
  rc = tme_eth_init(element,
		    TME_INVALID_HANDLE,
		    TME_ETHERNET_FRAME_MAX,
		    udp,
		    NULL);
  if (rc != TME_OK) {
    close(fd);
    tme_free(udp);
    return (rc);
  }

  udp->tme_udp_eth = (struct tme_ethernet *) element->tme_element_private;
  udp->tme_udp_eth->tme_eth_buffer = udp->tme_udp_buffer_in;
  udp->tme_udp_eth->tme_eth_out = udp->tme_udp_buffer_out;
  udp->tme_udp_eth->tme_ethernet_read = _tme_udp_read;
  udp->tme_udp_eth->tme_ethernet_write = _tme_udp_write;
  tme_eth_start(udp->tme_udp_eth);

  return (TME_OK);
}
