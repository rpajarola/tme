/* tme/win32-compat.h - native Windows (mingw-w64): what POSIX has and
   Windows lacks or spells differently.  tme/common.h includes this. */

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

#ifndef _TME_WIN32_COMPAT_H
#define _TME_WIN32_COMPAT_H

#ifdef WIN32

/* winsock2.h must come before windows.h: */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

/* open(2) has no non-blocking mode; pipes and consoles are waited on
   by event_wait() instead: */
#ifndef O_NONBLOCK
#define O_NONBLOCK		0
#endif

/* mkdir(2) takes no mode: */
#define mkdir(path, mode)	_mkdir(path)

/* realpath(3), only with a NULL buffer; like realpath, it fails for a
   file that doesn't exist: */
#define realpath(path, buf)	tme_win32_realpath(path)
char *tme_win32_realpath(const char *path);

/* setenv(3), always overwriting: */
#define setenv(name, value, overwrite)	_putenv_s(name, value)

/* pread(2) and pwrite(2), without their atomicity: */
#define pread(fd, buf, count, offset)	tme_win32_pread(fd, buf, count, offset)
#define pwrite(fd, buf, count, offset)	tme_win32_pwrite(fd, buf, count, offset)
ssize_t tme_win32_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t tme_win32_pwrite(int fd, const void *buf, size_t count, off_t offset);

/* random(3) and srandom(3).  rand() gives only 15 bits, and this is
   a macro, for the tools that don't link with libtme: */
#define random()		((((long) rand() << 30) ^ ((long) rand() << 15) ^ rand()) & 0x7fffffff)
#define srandom(seed)		srand(seed)

/* fmemopen(3), only for reading: */
#define fmemopen(buf, size, mode)	tme_win32_fmemopen(buf, size, mode)
FILE *tme_win32_fmemopen(void *buf, size_t size, const char *mode);

/* this starts a program, without waiting for it: */
int tme_win32_spawn(const char *program, const char *const *argv, int detached);

/* a file chooser: this returns a file descriptor that the paths
   chosen can be read from, one per line, until it is closed: */
int tme_win32_choose(const char *title, const char *start, int folder, int multiple, const char *save_name);

/* this reads a pipe without blocking, failing with EAGAIN when it is
   empty: */
ssize_t tme_win32_read_nb(int fd, void *buf, size_t len);

/* this opens a file with the program the desktop uses for it: */
int tme_win32_open_file(const char *path);

/* this puts standard input, if it's a pipe or (with raw) a console,
   behind a thread, so that event_wait() can wait for it: */
void tme_win32_stdin_pump(int raw);

/* Winsock must be started before sockets are made; tmesh does it: */
int tme_win32_sockets_init(void);

#endif /* WIN32 */

#endif /* !_TME_WIN32_COMPAT_H */
