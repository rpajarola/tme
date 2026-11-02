/* tme/socket.h - BSD sockets or Winsock: */

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

#ifndef _TME_SOCKET_H
#define _TME_SOCKET_H

#include <tme/common.h>

/* sockets are file descriptors with BSD sockets, but not with
   Winsock, where they must be closed with closesocket(), errors come
   from WSAGetLastError(), and non-blocking mode is set with
   ioctlsocket().  these hide the difference: */
#ifdef WIN32

/* tme/win32-compat.h has included winsock2.h and ws2tcpip.h: */
#define tme_socket_close(s)		closesocket(s)
#define tme_socket_errno()		WSAGetLastError()
#define TME_SOCKET_EWOULDBLOCK		WSAEWOULDBLOCK
#define TME_SOCKET_EINTR		WSAEINTR
#define TME_SOCKET_EINPROGRESS		WSAEWOULDBLOCK

static _tme_inline int
tme_socket_set_nonblocking(int s)
{
  u_long one = 1;
  return (ioctlsocket(s, FIONBIO, &one));
}

#else  /* !WIN32 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#define tme_socket_close(s)		close(s)
#define tme_socket_errno()		(errno)
#define TME_SOCKET_EWOULDBLOCK		EWOULDBLOCK
#define TME_SOCKET_EINTR		EINTR
#define TME_SOCKET_EINPROGRESS		EINPROGRESS

static _tme_inline int
tme_socket_set_nonblocking(int s)
{
  return (fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK));
}

#endif /* !WIN32 */

/* nonzero if a socket call failed only because it would have
   blocked: */
#ifdef EAGAIN
#define tme_socket_would_block(e)	((e) == TME_SOCKET_EWOULDBLOCK || (e) == EAGAIN)
#else  /* !EAGAIN */
#define tme_socket_would_block(e)	((e) == TME_SOCKET_EWOULDBLOCK)
#endif /* !EAGAIN */

#endif /* !_TME_SOCKET_H */
