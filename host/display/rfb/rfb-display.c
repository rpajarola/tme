/* host/display/rfb/rfb-display.c - the VNC (RFB) display: */

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

/* the VNC display, tme/host/rfb/display [ port PORT ] [ address
   ADDRESS ] | [ socket PATH ], shows the first screen to VNC clients
   (RFB 3.3, 3.7 and 3.8), instead of in a window.  without an address,
   it listens on this computer only, and without a port, on the first
   free one from 5900 up.
   clients need a password (VNC Authentication) if TME_VNC_PASSWORD is
   set, and TME_VNC_DEBUG traces the protocol.

   the display thread calls _tme_rfb_display_update() between screen
   updates.  it accepts clients and reads what they sent, without the
   mutex, so keys (X11 keysyms, like the SDL display's) and the mouse
   (whose motion is made from the change in the position) go to the
   generic keyboard and mouse support, and then, with the mutex, sends
   the clients what they asked for.  with cooperative threads, that can
   start over from the top, so everything is kept here.

   a screen is translated to 0x00RRGGBB pixels at its own size.  each
   time it changes, its 64x64 tiles are compared with what each client
   was sent last, and the ones that differ are sent as Raw rectangles,
   or, with zlib, as ZRLE (which makes text screens tiny) or Zlib,
   whichever the client lists first (macOS's Screen Sharing wants
   Zlib), when the client has asked for an update, and what it was sent
   before has gone out: */

#include <tme/common.h>

/* includes: */
#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif /* HAVE_ZLIB */

/* macros: */
#define TME_RFB_MAX_CLIENTS		(4)
#define TME_RFB_TILE			(64)
#define TME_RFB_MAX_MESSAGE		(4 + 4 * 65536)
#define TME_RFB_PORT_FIRST		(5900)
#define TME_RFB_PORT_LAST		(5999)

/* encodings: */
#define TME_RFB_ENC_RAW			(0)
#define TME_RFB_ENC_ZLIB		(6)
#define TME_RFB_ENC_ZRLE		(16)
#define TME_RFB_ENC_DESKTOP_SIZE	(-223)
#define TME_RFB_ENC_DESKTOP_NAME	(-307)

/* the client states: */
#define TME_RFB_STATE_VERSION		(0)	/* waiting for its version */
#define TME_RFB_STATE_SECURITY		(1)	/* ... its security type */
#define TME_RFB_STATE_AUTH		(2)	/* ... its answer to the challenge */
#define TME_RFB_STATE_CLIENT_INIT	(3)	/* ... ClientInit */
#define TME_RFB_STATE_WAIT_SCREEN	(4)	/* ... a screen to show */
#define TME_RFB_STATE_NORMAL		(5)

/* the protocol, as it happens, with TME_VNC_DEBUG set: */
#define TME_RFB_TRACE(display, client, ...)				\
  do {									\
    if ((display)->tme_rfb_display_trace) {				\
      fprintf(stderr, "vnc[%d]: ",					\
	      (int) ((client) - (display)->tme_rfb_display_clients));	\
      fprintf(stderr, __VA_ARGS__);					\
      fputc('\n', stderr);						\
    }									\
  } while (/* CONSTCOND */ 0)

/* types: */

/* a screen: */
struct tme_rfb_screen {

  /* the generic screen structure: */
  struct tme_screen screen;

  /* the translated pixels, 0x00RRGGBB, of the allocated size, and the
     size shown: */
  tme_uint32_t *tme_rfb_screen_pixels;
  size_t tme_rfb_screen_pixels_size;
  int tme_rfb_screen_stride;
  int tme_rfb_screen_width;
  int tme_rfb_screen_height;

  /* this changes each time the pixels change: */
  unsigned long tme_rfb_screen_generation;
};

/* a client: */
struct tme_rfb_client {

  /* the socket, or -1 for a free slot: */
  int tme_rfb_client_fd;
  int tme_rfb_client_state;
  int tme_rfb_client_minor;

  /* what it sent, and how much of a ClientCutText is to be skipped: */
  tme_uint8_t *tme_rfb_client_in;
  size_t tme_rfb_client_inlen;
  size_t tme_rfb_client_skip;

  /* what is to be sent to it: */
  tme_uint8_t *tme_rfb_client_out;
  size_t tme_rfb_client_outlen;
  size_t tme_rfb_client_outsize;

  /* its pixel format: */
  int tme_rfb_client_bytes;
  int tme_rfb_client_big_endian;
  tme_uint32_t tme_rfb_client_rtab[256];
  tme_uint32_t tme_rfb_client_gtab[256];
  tme_uint32_t tme_rfb_client_btab[256];
  int tme_rfb_client_cbytes;
  int tme_rfb_client_chigh;

  /* what it can take: */
  tme_int32_t tme_rfb_client_encoding;
  int tme_rfb_client_desktop_size;
  int tme_rfb_client_desktop_name;
#ifdef HAVE_ZLIB
  z_stream tme_rfb_client_zs[2];	/* ZRLE's and Zlib's */
  int tme_rfb_client_zs_init[2];
#endif /* HAVE_ZLIB */

  /* what it shows: */
  struct tme_rfb_screen *tme_rfb_client_screen;
  int tme_rfb_client_width;
  int tme_rfb_client_height;
  char *tme_rfb_client_name;
  int tme_rfb_client_tiles_x;
  int tme_rfb_client_tiles_y;
  tme_uint8_t *tme_rfb_client_dirty;
  tme_uint32_t *tme_rfb_client_shadow;
  unsigned long tme_rfb_client_generation;
  int tme_rfb_client_update_requested;
  int tme_rfb_client_resized;

  /* the password challenge, and whether to close once what is sent
     has gone: */
  tme_uint8_t tme_rfb_client_challenge[16];
  int tme_rfb_client_closing;
};

/* the display: */
struct tme_rfb_display {

  /* the generic display structure: */
  struct tme_display display;

  /* the listening socket, and a Unix socket's path: */
  int tme_rfb_display_listen_fd;
  char *tme_rfb_display_unix_path;

  /* the clients: */
  struct tme_rfb_client tme_rfb_display_clients[TME_RFB_MAX_CLIENTS];

  /* any password, and when the last wrong one was given: */
  char *tme_rfb_display_password;
  int tme_rfb_display_wrong_password;
  tme_uint32_t tme_rfb_display_wrong_password_at;

  /* nonzero to trace the protocol: */
  int tme_rfb_display_trace;

  /* the number of bells to send: */
  unsigned int tme_rfb_display_bells;

  /* where a rectangle is put together: */
  tme_uint8_t *tme_rfb_display_scratch;
  size_t tme_rfb_display_scratch_size;
};

/* the Unix sockets to remove at exit: */
static char *_tme_rfb_unix_paths[4];

/* this removes the Unix sockets: */
static void
_tme_rfb_remove_unix_sockets(void)
{
  unsigned int path_i;

  for (path_i = 0; path_i < TME_ARRAY_ELS(_tme_rfb_unix_paths); path_i++) {
    if (_tme_rfb_unix_paths[path_i] != NULL) {
      unlink(_tme_rfb_unix_paths[path_i]);
    }
  }
}

/* sending: everything goes to a client's out buffer, which
   _tme_rfb_flush() sends as far as the socket takes it: */

/* this drops a client: */
static void
_tme_rfb_disconnect(struct tme_rfb_display *display,
		    struct tme_rfb_client *client,
		    const char *why)
{
#ifdef HAVE_ZLIB
  int i;
#endif /* HAVE_ZLIB */

  if (client->tme_rfb_client_fd < 0) {
    return;
  }
  if (why != NULL) {
    tme_log(&display->display.tme_display_element->tme_element_log_handle,
	    0, TME_OK,
	    (&display->display.tme_display_element->tme_element_log_handle,
	     _("dropping a VNC client: %s"), why));
  }
  TME_RFB_TRACE(display, client, "disconnected%s%s",
		(why != NULL ? ": " : ""), (why != NULL ? why : ""));

  close(client->tme_rfb_client_fd);
  tme_free(client->tme_rfb_client_in);
  if (client->tme_rfb_client_out != NULL) {
    tme_free(client->tme_rfb_client_out);
  }
  if (client->tme_rfb_client_name != NULL) {
    tme_free(client->tme_rfb_client_name);
  }
  if (client->tme_rfb_client_dirty != NULL) {
    tme_free(client->tme_rfb_client_dirty);
  }
  if (client->tme_rfb_client_shadow != NULL) {
    tme_free(client->tme_rfb_client_shadow);
  }
#ifdef HAVE_ZLIB
  for (i = 0; i < 2; i++) {
    if (client->tme_rfb_client_zs_init[i]) {
      deflateEnd(&client->tme_rfb_client_zs[i]);
    }
  }
#endif /* HAVE_ZLIB */
  memset(client, 0, sizeof(*client));
  client->tme_rfb_client_fd = -1;
}

/* this makes room for n more bytes to send: */
static void
_tme_rfb_reserve(struct tme_rfb_client *client, size_t n)
{
  size_t size;

  size = client->tme_rfb_client_outsize;
  if (client->tme_rfb_client_outlen + n <= size) {
    return;
  }
  while (client->tme_rfb_client_outlen + n > size) {
    size = (size == 0 ? 65536 : size * 2);
  }
  client->tme_rfb_client_out
    = (client->tme_rfb_client_out == NULL
       ? tme_new(tme_uint8_t, size)
       : tme_renew(tme_uint8_t, client->tme_rfb_client_out, size));
  client->tme_rfb_client_outsize = size;
}

static void
_tme_rfb_put(struct tme_rfb_client *client, const void *data, size_t n)
{
  _tme_rfb_reserve(client, n);
  memcpy(client->tme_rfb_client_out + client->tme_rfb_client_outlen, data, n);
  client->tme_rfb_client_outlen += n;
}

static void
_tme_rfb_put8(struct tme_rfb_client *client, unsigned int v)
{
  tme_uint8_t b;

  b = v;
  _tme_rfb_put(client, &b, 1);
}

static void
_tme_rfb_put16(struct tme_rfb_client *client, unsigned int v)
{
  tme_uint8_t b[2];

  b[0] = v >> 8;
  b[1] = v;
  _tme_rfb_put(client, b, 2);
}

static void
_tme_rfb_put32(struct tme_rfb_client *client, tme_uint32_t v)
{
  tme_uint8_t b[4];

  b[0] = v >> 24;
  b[1] = v >> 16;
  b[2] = v >> 8;
  b[3] = v;
  _tme_rfb_put(client, b, 4);
}

/* this sends what it can: */
static void
_tme_rfb_flush(struct tme_rfb_display *display,
	       struct tme_rfb_client *client)
{
  size_t done;
  ssize_t n;
  int flags;

#ifdef MSG_NOSIGNAL
  flags = MSG_NOSIGNAL;
#else
  flags = 0;
#endif

  if (client->tme_rfb_client_fd < 0) {
    return;
  }

  done = 0;
  while (done < client->tme_rfb_client_outlen) {
    n = send(client->tme_rfb_client_fd,
	     client->tme_rfb_client_out + done,
	     client->tme_rfb_client_outlen - done,
	     flags);
    if (n > 0) {
      done += n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    _tme_rfb_disconnect(display, client, NULL);
    return;
  }

  memmove(client->tme_rfb_client_out,
	  client->tme_rfb_client_out + done,
	  client->tme_rfb_client_outlen - done);
  client->tme_rfb_client_outlen -= done;
}

/* pixels: a screen's 0x00RRGGBB pixels, in a client's format: */

/* this sets a client's pixel format: */
static void
_tme_rfb_pixel_format(struct tme_rfb_client *client,
		      int bpp, int depth, int big_endian,
		      int rmax, int gmax, int bmax,
		      int rshift, int gshift, int bshift)
{
  tme_uint32_t mask;
  int i;

  client->tme_rfb_client_bytes = bpp / 8;
  client->tme_rfb_client_big_endian = big_endian;
  for (i = 0; i < 256; i++) {
    client->tme_rfb_client_rtab[i] = ((tme_uint32_t) ((i * rmax + 127) / 255)) << rshift;
    client->tme_rfb_client_gtab[i] = ((tme_uint32_t) ((i * gmax + 127) / 255)) << gshift;
    client->tme_rfb_client_btab[i] = ((tme_uint32_t) ((i * bmax + 127) / 255)) << bshift;
  }

  /* ZRLE sends 32-bit pixels which fit in 3 bytes as 3 bytes: */
  mask = (client->tme_rfb_client_rtab[255]
	  | client->tme_rfb_client_gtab[255]
	  | client->tme_rfb_client_btab[255]);
  client->tme_rfb_client_cbytes = client->tme_rfb_client_bytes;
  client->tme_rfb_client_chigh = FALSE;
  if (bpp == 32 && depth <= 24) {
    if ((mask & 0xff000000) == 0) {
      client->tme_rfb_client_cbytes = 3;
    }
    else if ((mask & 0xff) == 0) {
      client->tme_rfb_client_cbytes = 3;
      client->tme_rfb_client_chigh = TRUE;
    }
  }
}

static inline tme_uint32_t
_tme_rfb_convert(const struct tme_rfb_client *client, tme_uint32_t rgb)
{
  return (client->tme_rfb_client_rtab[(rgb >> 16) & 0xff]
	  | client->tme_rfb_client_gtab[(rgb >> 8) & 0xff]
	  | client->tme_rfb_client_btab[rgb & 0xff]);
}

/* this writes a pixel of n bytes, from the low or high end of v: */
static tme_uint8_t *
_tme_rfb_pixel_bytes(const struct tme_rfb_client *client,
		     tme_uint8_t *p, tme_uint32_t v, int n, int high)
{
  int i;

  if (high) {
    v >>= 8;
  }
  if (client->tme_rfb_client_big_endian) {
    for (i = n - 1; i >= 0; i--) {
      *(p++) = v >> (8 * i);
    }
  }
  else {
    for (i = 0; i < n; i++) {
      *(p++) = v >> (8 * i);
    }
  }
  return (p);
}

static void
_tme_rfb_rect_header(struct tme_rfb_client *client,
		     int x, int y, int w, int h, tme_int32_t encoding)
{
  _tme_rfb_put16(client, x);
  _tme_rfb_put16(client, y);
  _tme_rfb_put16(client, w);
  _tme_rfb_put16(client, h);
  _tme_rfb_put32(client, (tme_uint32_t) encoding);
}

/* this writes the pixels of a rectangle, as for Raw: */
static tme_uint8_t *
_tme_rfb_raw_pixels(const struct tme_rfb_client *client, tme_uint8_t *p,
		    int x, int y, int w, int h)
{
  const tme_uint32_t *src;
  int xx, yy;

  for (yy = y; yy < y + h; yy++) {
    src = client->tme_rfb_client_shadow + (size_t) yy * client->tme_rfb_client_width + x;
    for (xx = 0; xx < w; xx++) {
      p = _tme_rfb_pixel_bytes(client, p, _tme_rfb_convert(client, src[xx]),
			       client->tme_rfb_client_bytes, FALSE);
    }
  }
  return (p);
}

static void
_tme_rfb_send_raw(struct tme_rfb_client *client, int x, int y, int w, int h)
{
  tme_uint8_t *p;

  _tme_rfb_rect_header(client, x, y, w, h, TME_RFB_ENC_RAW);
  _tme_rfb_reserve(client, (size_t) w * h * client->tme_rfb_client_bytes);
  p = _tme_rfb_raw_pixels(client,
			  client->tme_rfb_client_out + client->tme_rfb_client_outlen,
			  x, y, w, h);
  client->tme_rfb_client_outlen = p - client->tme_rfb_client_out;
}

#ifdef HAVE_ZLIB

/* compressed encodings: Zlib is the pixels of a rectangle, as for Raw,
   compressed with the client's zlib stream for Zlib.  in ZRLE, a
   rectangle is cut into 64x64 tiles, each of which is sent as one
   colour, a packed palette, run lengths (of colours or of palette
   indices), or raw, whichever is the smallest, and all the tiles
   together are then compressed with the client's zlib stream for
   ZRLE: */

#define TME_RFB_ZRLE_MAX_PALETTE	(127)

struct tme_rfb_palette {
  int n;
  tme_uint32_t colors[TME_RFB_ZRLE_MAX_PALETTE];
  struct {
    tme_uint32_t color;
    int index;			/* -1 if unused */
  } hash[256];
};

/* this returns at least n bytes of scratch space: */
static tme_uint8_t *
_tme_rfb_scratch(struct tme_rfb_display *display, size_t n)
{
  if (n > display->tme_rfb_display_scratch_size) {
    if (display->tme_rfb_display_scratch != NULL) {
      tme_free(display->tme_rfb_display_scratch);
    }
    display->tme_rfb_display_scratch = tme_new(tme_uint8_t, n);
    display->tme_rfb_display_scratch_size = n;
  }
  return (display->tme_rfb_display_scratch);
}

/* this sends data compressed with zlib stream i, after its length: */
static void
_tme_rfb_put_deflated(struct tme_rfb_client *client, int i,
		      tme_uint8_t *data, size_t n)
{
  z_stream *zs;
  size_t lenpos;
  tme_uint32_t len;

  zs = &client->tme_rfb_client_zs[i];
  if (!client->tme_rfb_client_zs_init[i]) {
    if (deflateInit(zs, Z_DEFAULT_COMPRESSION) != Z_OK) {
      abort();
    }
    client->tme_rfb_client_zs_init[i] = TRUE;
  }

  lenpos = client->tme_rfb_client_outlen;
  _tme_rfb_put32(client, 0);

  zs->next_in = data;
  zs->avail_in = n;
  do {
    _tme_rfb_reserve(client, 65536);
    zs->next_out = client->tme_rfb_client_out + client->tme_rfb_client_outlen;
    zs->avail_out = client->tme_rfb_client_outsize - client->tme_rfb_client_outlen;
    deflate(zs, Z_SYNC_FLUSH);
    client->tme_rfb_client_outlen = client->tme_rfb_client_outsize - zs->avail_out;
  } while (zs->avail_out == 0);

  len = client->tme_rfb_client_outlen - lenpos - 4;
  client->tme_rfb_client_out[lenpos] = len >> 24;
  client->tme_rfb_client_out[lenpos + 1] = len >> 16;
  client->tme_rfb_client_out[lenpos + 2] = len >> 8;
  client->tme_rfb_client_out[lenpos + 3] = len;
}

static int
_tme_rfb_palette_index(struct tme_rfb_palette *pal, tme_uint32_t color)
{
  unsigned int h;

  h = (color * 2654435761u) >> 24;
  for (;;) {
    if (pal->hash[h].index < 0) {
      if (pal->n >= TME_RFB_ZRLE_MAX_PALETTE) {
	return (-1);
      }
      pal->hash[h].color = color;
      pal->hash[h].index = pal->n;
      pal->colors[pal->n] = color;
      return (pal->n++);
    }
    if (pal->hash[h].color == color) {
      return (pal->hash[h].index);
    }
    h = (h + 1) & 255;
  }
}

static tme_uint8_t *
_tme_rfb_put_cpixel(const struct tme_rfb_client *client, tme_uint8_t *p, tme_uint32_t v)
{
  return (_tme_rfb_pixel_bytes(client, p, v,
			       client->tme_rfb_client_cbytes,
			       client->tme_rfb_client_chigh));
}

/* this writes a run length, as ZRLE does: */
static tme_uint8_t *
_tme_rfb_put_run(tme_uint8_t *p, int len)
{
  len--;
  while (len >= 255) {
    *(p++) = 255;
    len -= 255;
  }
  *(p++) = len;
  return (p);
}

/* this writes one ZRLE tile: */
static tme_uint8_t *
_tme_rfb_zrle_tile(const struct tme_rfb_client *client, tme_uint8_t *p,
		   int x0, int y0, int w, int h)
{
  static tme_uint32_t px[TME_RFB_TILE * TME_RFB_TILE];
  static tme_uint8_t idx[TME_RFB_TILE * TME_RFB_TILE];
  struct tme_rfb_palette pal;
  const tme_uint32_t *src;
  int n, cb, i, j, k, x, y, len, bits, byte, nbits;
  size_t plain_rle, palette_rle, packed, best;
  int overflow;
  enum { RAW, PACKED, PLAIN_RLE, PALETTE_RLE } kind;

  n = w * h;
  cb = client->tme_rfb_client_cbytes;
  for (y = 0; y < h; y++) {
    src = client->tme_rfb_client_shadow + (size_t) (y0 + y) * client->tme_rfb_client_width + x0;
    for (x = 0; x < w; x++) {
      px[y * w + x] = _tme_rfb_convert(client, src[x]);
    }
  }

  /* count the colours, and what each kind of run lengths takes: */
  plain_rle = 1;
  palette_rle = 1;
  overflow = FALSE;
  pal.n = 0;
  for (i = 0; i < 256; i++) {
    pal.hash[i].index = -1;
  }
  for (i = 0; i < n; ) {
    for (j = i + 1; j < n && px[j] == px[i]; j++);
    len = j - i;
    plain_rle += cb + (len - 1) / 255 + 1;
    palette_rle += (len == 1 ? 1 : 1 + (len - 1) / 255 + 1);
    if (!overflow) {
      k = _tme_rfb_palette_index(&pal, px[i]);
      if (k < 0) {
	overflow = TRUE;
      }
      else {
	memset(idx + i, k, len);
      }
    }
    i = j;
  }

  /* one colour: */
  if (!overflow && pal.n == 1) {
    *(p++) = 1;
    return (_tme_rfb_put_cpixel(client, p, px[0]));
  }

  /* otherwise, the smallest: */
  best = 1 + (size_t) n * cb;
  kind = RAW;
  bits = 0;
  if (plain_rle < best) {
    best = plain_rle;
    kind = PLAIN_RLE;
  }
  if (!overflow) {
    palette_rle += (size_t) pal.n * cb;
    if (palette_rle < best) {
      best = palette_rle;
      kind = PALETTE_RLE;
    }
    if (pal.n <= 16) {
      bits = (pal.n <= 2 ? 1 : pal.n <= 4 ? 2 : 4);
      packed = 1 + (size_t) pal.n * cb + (size_t) h * ((w * bits + 7) / 8);
      if (packed < best) {
	best = packed;
	kind = PACKED;
      }
    }
  }

  switch (kind) {

  case RAW:
    *(p++) = 0;
    for (i = 0; i < n; i++) {
      p = _tme_rfb_put_cpixel(client, p, px[i]);
    }
    break;

  case PACKED:
    *(p++) = pal.n;
    for (i = 0; i < pal.n; i++) {
      p = _tme_rfb_put_cpixel(client, p, pal.colors[i]);
    }
    for (y = 0; y < h; y++) {
      byte = 0;
      nbits = 0;
      for (x = 0; x < w; x++) {
	byte = (byte << bits) | idx[y * w + x];
	nbits += bits;
	if (nbits == 8) {
	  *(p++) = byte;
	  byte = 0;
	  nbits = 0;
	}
      }
      if (nbits > 0) {
	*(p++) = byte << (8 - nbits);
      }
    }
    break;

  case PLAIN_RLE:
    *(p++) = 128;
    for (i = 0; i < n; ) {
      for (j = i + 1; j < n && px[j] == px[i]; j++);
      p = _tme_rfb_put_cpixel(client, p, px[i]);
      p = _tme_rfb_put_run(p, j - i);
      i = j;
    }
    break;

  case PALETTE_RLE:
    *(p++) = 128 + pal.n;
    for (i = 0; i < pal.n; i++) {
      p = _tme_rfb_put_cpixel(client, p, pal.colors[i]);
    }
    for (i = 0; i < n; ) {
      for (j = i + 1; j < n && px[j] == px[i]; j++);
      if (j - i == 1) {
	*(p++) = idx[i];
      }
      else {
	*(p++) = idx[i] | 128;
	p = _tme_rfb_put_run(p, j - i);
      }
      i = j;
    }
    break;
  }

  return (p);
}

static void
_tme_rfb_send_zrle(struct tme_rfb_display *display,
		   struct tme_rfb_client *client,
		   int x, int y, int w, int h)
{
  size_t tiles;
  tme_uint8_t *start, *p;
  int tx, ty;

  /* (the worst case for a tile is raw, plus its subencoding byte.) */
  tiles = ((size_t) ((w + TME_RFB_TILE - 1) / TME_RFB_TILE)
	   * ((h + TME_RFB_TILE - 1) / TME_RFB_TILE));
  start = _tme_rfb_scratch(display, tiles * (1 + TME_RFB_TILE * TME_RFB_TILE * 4));
  p = start;
  for (ty = y; ty < y + h; ty += TME_RFB_TILE) {
    for (tx = x; tx < x + w; tx += TME_RFB_TILE) {
      p = _tme_rfb_zrle_tile(client, p, tx, ty,
			     TME_MIN(x + w - tx, TME_RFB_TILE),
			     TME_MIN(y + h - ty, TME_RFB_TILE));
    }
  }

  _tme_rfb_rect_header(client, x, y, w, h, TME_RFB_ENC_ZRLE);
  _tme_rfb_put_deflated(client, 0, start, p - start);
}

static void
_tme_rfb_send_zlib(struct tme_rfb_display *display,
		   struct tme_rfb_client *client,
		   int x, int y, int w, int h)
{
  tme_uint8_t *start, *p;

  start = _tme_rfb_scratch(display, (size_t) w * h * 4);
  p = _tme_rfb_raw_pixels(client, start, x, y, w, h);
  _tme_rfb_rect_header(client, x, y, w, h, TME_RFB_ENC_ZLIB);
  _tme_rfb_put_deflated(client, 1, start, p - start);
}

#endif /* HAVE_ZLIB */

/* updates: */

/* this returns the screen that clients show, the first one there is,
   or NULL: */
static struct tme_rfb_screen *
_tme_rfb_shown_screen(struct tme_rfb_display *display)
{
  struct tme_screen *screen, *first;

  /* (new screens are added at the front:) */
  first = NULL;
  for (screen = display->display.tme_display_screens;
       screen != NULL;
       screen = screen->tme_screen_next) {
    if (((struct tme_rfb_screen *) screen)->tme_rfb_screen_pixels != NULL) {
      first = screen;
    }
  }
  return ((struct tme_rfb_screen *) first);
}

/* this takes on the screen's size, with everything to be sent: */
static void
_tme_rfb_take_size(struct tme_rfb_client *client)
{
  struct tme_rfb_screen *screen;
  size_t tiles;

  screen = client->tme_rfb_client_screen;
  client->tme_rfb_client_width = screen->tme_rfb_screen_width;
  client->tme_rfb_client_height = screen->tme_rfb_screen_height;
  client->tme_rfb_client_tiles_x = (client->tme_rfb_client_width + TME_RFB_TILE - 1) / TME_RFB_TILE;
  client->tme_rfb_client_tiles_y = (client->tme_rfb_client_height + TME_RFB_TILE - 1) / TME_RFB_TILE;
  tiles = (size_t) client->tme_rfb_client_tiles_x * client->tme_rfb_client_tiles_y;

  if (client->tme_rfb_client_dirty != NULL) {
    tme_free(client->tme_rfb_client_dirty);
  }
  if (client->tme_rfb_client_shadow != NULL) {
    tme_free(client->tme_rfb_client_shadow);
  }
  client->tme_rfb_client_dirty = tme_new(tme_uint8_t, TME_MAX(tiles, 1));
  memset(client->tme_rfb_client_dirty, 2, tiles);
  client->tme_rfb_client_shadow
    = tme_new0(tme_uint32_t,
	       TME_MAX((size_t) client->tme_rfb_client_width * client->tme_rfb_client_height, 1));
  client->tme_rfb_client_generation = screen->tme_rfb_screen_generation;
}

/* this sends ServerInit, with the mutex locked: */
static void
_tme_rfb_server_init(struct tme_rfb_display *display,
		     struct tme_rfb_client *client,
		     struct tme_rfb_screen *screen)
{
  client->tme_rfb_client_screen = screen;
  _tme_rfb_take_size(client);
  client->tme_rfb_client_name = tme_strdup(display->display.tme_display_title);

  _tme_rfb_put16(client, client->tme_rfb_client_width);
  _tme_rfb_put16(client, client->tme_rfb_client_height);

  /* 32 bits per pixel, depth 24, little-endian, true colour: */
  _tme_rfb_put8(client, 32);
  _tme_rfb_put8(client, 24);
  _tme_rfb_put8(client, 0);
  _tme_rfb_put8(client, 1);
  _tme_rfb_put16(client, 255);
  _tme_rfb_put16(client, 255);
  _tme_rfb_put16(client, 255);
  _tme_rfb_put8(client, 16);
  _tme_rfb_put8(client, 8);
  _tme_rfb_put8(client, 0);
  _tme_rfb_put8(client, 0);
  _tme_rfb_put16(client, 0);
  _tme_rfb_pixel_format(client, 32, 24, FALSE, 255, 255, 255, 16, 8, 0);

  _tme_rfb_put32(client, strlen(client->tme_rfb_client_name));
  _tme_rfb_put(client, client->tme_rfb_client_name, strlen(client->tme_rfb_client_name));
  TME_RFB_TRACE(display, client, "ServerInit: %dx%d, \"%s\"",
		client->tme_rfb_client_width, client->tme_rfb_client_height,
		client->tme_rfb_client_name);

  client->tme_rfb_client_state = TME_RFB_STATE_NORMAL;
}

/* this copies the tiles which changed into the shadow image.  a tile
   is 1 when it may have changed, and 2 when it is to be sent even if
   it looks the same; afterwards, the ones to send are 3, and the rest
   0.  it returns the number of tiles to send: */
static int
_tme_rfb_changed_tiles(struct tme_rfb_client *client)
{
  struct tme_rfb_screen *screen;
  tme_uint8_t *d;
  int n, tx, ty, x, y, w, h, yy, changed;
  size_t o, so;

  screen = client->tme_rfb_client_screen;
  n = 0;
  for (ty = 0; ty < client->tme_rfb_client_tiles_y; ty++) {
    for (tx = 0; tx < client->tme_rfb_client_tiles_x; tx++) {
      d = &client->tme_rfb_client_dirty[ty * client->tme_rfb_client_tiles_x + tx];
      if (*d == 0) {
	continue;
      }
      x = tx * TME_RFB_TILE;
      y = ty * TME_RFB_TILE;
      w = TME_MIN(client->tme_rfb_client_width - x, TME_RFB_TILE);
      h = TME_MIN(client->tme_rfb_client_height - y, TME_RFB_TILE);
      changed = (*d == 2);
      for (yy = y; yy < y + h; yy++) {
	o = (size_t) yy * client->tme_rfb_client_width + x;
	so = (size_t) yy * screen->tme_rfb_screen_stride + x;
	if (!changed
	    && memcmp(client->tme_rfb_client_shadow + o,
		      screen->tme_rfb_screen_pixels + so,
		      w * sizeof(tme_uint32_t)) == 0) {
	  continue;
	}
	memcpy(client->tme_rfb_client_shadow + o,
	       screen->tme_rfb_screen_pixels + so,
	       w * sizeof(tme_uint32_t));
	changed = TRUE;
      }
      *d = (changed ? 3 : 0);
      n += changed;
    }
  }
  return (n);
}

/* this sends an update, with the mutex locked: */
static void
_tme_rfb_send_update(struct tme_rfb_display *display,
		     struct tme_rfb_client *client)
{
  struct tme_rfb_screen *screen;
  size_t countpos, tiles, tile_i;
  int rename, n, nrects, tx, ty, tx2, x, y, x2, y2;

  screen = client->tme_rfb_client_screen;

  /* a new size: */
  if (screen->tme_rfb_screen_width != client->tme_rfb_client_width
      || screen->tme_rfb_screen_height != client->tme_rfb_client_height) {
    if (!client->tme_rfb_client_desktop_size) {
      _tme_rfb_disconnect(display, client,
			  "the screen size changed, and the client can't follow");
      return;
    }
    _tme_rfb_take_size(client);
    client->tme_rfb_client_resized = TRUE;
  }

  /* if the screen changed, any tile may have: */
  if (client->tme_rfb_client_generation != screen->tme_rfb_screen_generation) {
    tiles = (size_t) client->tme_rfb_client_tiles_x * client->tme_rfb_client_tiles_y;
    for (tile_i = 0; tile_i < tiles; tile_i++) {
      if (client->tme_rfb_client_dirty[tile_i] == 0) {
	client->tme_rfb_client_dirty[tile_i] = 1;
      }
    }
    client->tme_rfb_client_generation = screen->tme_rfb_screen_generation;
  }

  rename = (client->tme_rfb_client_desktop_name
	    && strcmp(client->tme_rfb_client_name, display->display.tme_display_title) != 0);
  n = _tme_rfb_changed_tiles(client);
  if (n == 0 && !client->tme_rfb_client_resized && !rename) {
    return;
  }

  _tme_rfb_put8(client, 0);
  _tme_rfb_put8(client, 0);
  countpos = client->tme_rfb_client_outlen;
  _tme_rfb_put16(client, 0);
  nrects = 0;

  if (client->tme_rfb_client_resized) {
    _tme_rfb_rect_header(client, 0, 0,
			 client->tme_rfb_client_width, client->tme_rfb_client_height,
			 TME_RFB_ENC_DESKTOP_SIZE);
    nrects++;
    client->tme_rfb_client_resized = FALSE;
  }

  if (rename) {
    tme_free(client->tme_rfb_client_name);
    client->tme_rfb_client_name = tme_strdup(display->display.tme_display_title);
    _tme_rfb_rect_header(client, 0, 0, 0, 0, TME_RFB_ENC_DESKTOP_NAME);
    _tme_rfb_put32(client, strlen(client->tme_rfb_client_name));
    _tme_rfb_put(client, client->tme_rfb_client_name, strlen(client->tme_rfb_client_name));
    nrects++;
  }

  /* a rectangle for each run of tiles in a row: */
  for (ty = 0; ty < client->tme_rfb_client_tiles_y; ty++) {
    for (tx = 0; tx < client->tme_rfb_client_tiles_x; ) {
      if (client->tme_rfb_client_dirty[ty * client->tme_rfb_client_tiles_x + tx] != 3) {
	tx++;
	continue;
      }
      for (tx2 = tx;
	   (tx2 < client->tme_rfb_client_tiles_x
	    && client->tme_rfb_client_dirty[ty * client->tme_rfb_client_tiles_x + tx2] == 3);
	   tx2++) {
	client->tme_rfb_client_dirty[ty * client->tme_rfb_client_tiles_x + tx2] = 0;
      }
      x = tx * TME_RFB_TILE;
      y = ty * TME_RFB_TILE;
      x2 = TME_MIN(tx2 * TME_RFB_TILE, client->tme_rfb_client_width);
      y2 = TME_MIN(y + TME_RFB_TILE, client->tme_rfb_client_height);
#ifdef HAVE_ZLIB
      if (client->tme_rfb_client_encoding == TME_RFB_ENC_ZRLE) {
	_tme_rfb_send_zrle(display, client, x, y, x2 - x, y2 - y);
      }
      else if (client->tme_rfb_client_encoding == TME_RFB_ENC_ZLIB) {
	_tme_rfb_send_zlib(display, client, x, y, x2 - x, y2 - y);
      }
      else
#endif /* HAVE_ZLIB */
	_tme_rfb_send_raw(client, x, y, x2 - x, y2 - y);
      nrects++;
      tx = tx2;
    }
  }

  client->tme_rfb_client_out[countpos] = nrects >> 8;
  client->tme_rfb_client_out[countpos + 1] = nrects;
  TME_RFB_TRACE(display, client, "update: %d rectangles, %d tiles, %lu bytes (%s)",
		nrects, n, (unsigned long) client->tme_rfb_client_outlen,
		(client->tme_rfb_client_encoding == TME_RFB_ENC_ZRLE ? "ZRLE"
		 : client->tme_rfb_client_encoding == TME_RFB_ENC_ZLIB ? "Zlib"
		 : "Raw"));
  client->tme_rfb_client_update_requested = FALSE;
}

/* VNC Authentication: the server sends 16 random bytes, which the
   client encrypts with DES, with the password (up to 8 characters,
   each byte's bits reversed) as the key: */

static const tme_uint8_t _tme_rfb_des_ip[64] = {
  58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
  62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
  57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
  61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7 };
static const tme_uint8_t _tme_rfb_des_fp[64] = {
  40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
  38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
  36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
  34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25 };
static const tme_uint8_t _tme_rfb_des_e[48] = {
  32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11, 12, 13,
  12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21, 22, 23,
  24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1 };
static const tme_uint8_t _tme_rfb_des_p[32] = {
  16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
  2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25 };
static const tme_uint8_t _tme_rfb_des_pc1[56] = {
  57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18,
  10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36,
  63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22,
  14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4 };
static const tme_uint8_t _tme_rfb_des_pc2[48] = {
  14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10,
  23, 19, 12, 4, 26, 8, 16, 7, 27, 20, 13, 2,
  41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
  44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32 };
static const tme_uint8_t _tme_rfb_des_shifts[16] = {
  1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1 };
static const tme_uint8_t _tme_rfb_des_s[8][64] = {
  { 14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
    0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
    4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
    15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13 },
  { 15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
    3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
    0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
    13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9 },
  { 10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
    13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
    13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
    1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12 },
  { 7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
    13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
    10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
    3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14 },
  { 2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
    14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
    4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
    11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3 },
  { 12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
    10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
    9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
    4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13 },
  { 4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
    13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
    1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
    6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12 },
  { 13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
    1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
    7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
    2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11 } };

/* bit i (1 is the most significant) of the output is bit table[i-1]
   of the input: */
static tme_uint64_t
_tme_rfb_des_permute(tme_uint64_t in, int inbits, const tme_uint8_t *table, int n)
{
  tme_uint64_t out;
  int i;

  out = 0;
  for (i = 0; i < n; i++) {
    out = (out << 1) | ((in >> (inbits - table[i])) & 1);
  }
  return (out);
}

static tme_uint64_t
_tme_rfb_des_encrypt(tme_uint64_t key, tme_uint64_t block)
{
  tme_uint64_t k, b, subkey, x;
  tme_uint32_t c, d, l, r, f, t;
  int round, i, six;

  k = _tme_rfb_des_permute(key, 64, _tme_rfb_des_pc1, 56);
  c = k >> 28;
  d = k & 0xfffffff;
  b = _tme_rfb_des_permute(block, 64, _tme_rfb_des_ip, 64);
  l = b >> 32;
  r = (tme_uint32_t) b;

  for (round = 0; round < 16; round++) {
    for (i = 0; i < _tme_rfb_des_shifts[round]; i++) {
      c = ((c << 1) | (c >> 27)) & 0xfffffff;
      d = ((d << 1) | (d >> 27)) & 0xfffffff;
    }
    subkey = _tme_rfb_des_permute(((tme_uint64_t) c << 28) | d, 56, _tme_rfb_des_pc2, 48);
    x = _tme_rfb_des_permute(r, 32, _tme_rfb_des_e, 48) ^ subkey;
    f = 0;
    for (i = 0; i < 8; i++) {
      six = (x >> (42 - 6 * i)) & 63;
      f = (f << 4) | _tme_rfb_des_s[i][(six & 0x20) | ((six & 1) << 4) | ((six >> 1) & 15)];
    }
    f = _tme_rfb_des_permute(f, 32, _tme_rfb_des_p, 32);
    t = l ^ f;
    l = r;
    r = t;
  }

  return (_tme_rfb_des_permute(((tme_uint64_t) r << 32) | l, 64, _tme_rfb_des_fp, 64));
}

/* this returns nonzero if the client's answer is the challenge,
   encrypted with the password: */
static int
_tme_rfb_password_ok(const struct tme_rfb_display *display,
		     const struct tme_rfb_client *client,
		     const tme_uint8_t *p)
{
  const char *password;
  tme_uint64_t key, block, want;
  tme_uint8_t ch, rev, diff;
  int i, bit, half;

  password = display->tme_rfb_display_password;
  key = 0;
  for (i = 0; i < 8; i++) {
    ch = (i < (int) strlen(password) ? password[i] : 0);
    rev = 0;
    for (bit = 0; bit < 8; bit++) {
      if (ch & (1 << bit)) {
	rev |= 0x80 >> bit;
      }
    }
    key = (key << 8) | rev;
  }

  diff = 0;
  for (half = 0; half < 2; half++) {
    block = 0;
    for (i = 0; i < 8; i++) {
      block = (block << 8) | client->tme_rfb_client_challenge[half * 8 + i];
    }
    want = _tme_rfb_des_encrypt(key, block);
    for (i = 0; i < 8; i++) {
      diff |= p[half * 8 + i] ^ (tme_uint8_t) (want >> (56 - 8 * i));
    }
  }
  return (diff == 0);
}

static void
_tme_rfb_send_challenge(struct tme_rfb_client *client)
{
  FILE *f;
  int i;

  f = fopen("/dev/urandom", "rb");
  if (f == NULL
      || fread(client->tme_rfb_client_challenge, 1, 16, f) != 16) {
    for (i = 0; i < 16; i++) {
      client->tme_rfb_client_challenge[i] = random() >> 8;
    }
  }
  if (f != NULL) {
    fclose(f);
  }
  _tme_rfb_put(client, client->tme_rfb_client_challenge, 16);
  client->tme_rfb_client_state = TME_RFB_STATE_AUTH;
}

/* input: */

static tme_uint32_t
_tme_rfb_get16(const tme_uint8_t *p)
{
  return ((p[0] << 8) | p[1]);
}

static tme_uint32_t
_tme_rfb_get32(const tme_uint8_t *p)
{
  return (((tme_uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

/* this handles the message (or handshake step) at the start of p.  it
   returns how many bytes it was, zero if it isn't all there yet, or
   -1 if the client was dropped: */
static long
_tme_rfb_client_message(struct tme_rfb_display *display,
			struct tme_rfb_client *client,
			const tme_uint8_t *p, size_t n)
{
  const char *why;
  size_t len, i;
  tme_int32_t e;
  int chosen, x, y, w, h, tx, ty, client_i;
  tme_uint32_t keysym;
  char text[40];

  switch (client->tme_rfb_client_state) {

  case TME_RFB_STATE_VERSION:
    if (n < 12) {
      return (0);
    }
    if (memcmp(p, "RFB 003.", 8) != 0 || p[11] != '\n') {
      _tme_rfb_disconnect(display, client, "not a VNC client");
      return (-1);
    }
    TME_RFB_TRACE(display, client, "client version %.11s", (const char *) p);
    client->tme_rfb_client_minor = atoi((const char *) p + 8);
    if (client->tme_rfb_client_minor >= 8) {
      client->tme_rfb_client_minor = 8;
    }
    else if (client->tme_rfb_client_minor != 7) {
      client->tme_rfb_client_minor = 3;
    }

    /* the security type is VNC Authentication (2) with a password,
       and None (1) without: */
    if (client->tme_rfb_client_minor == 3) {
      _tme_rfb_put32(client, (display->tme_rfb_display_password != NULL ? 2 : 1));
      if (display->tme_rfb_display_password != NULL) {
	_tme_rfb_send_challenge(client);
      }
      else {
	client->tme_rfb_client_state = TME_RFB_STATE_CLIENT_INIT;
      }
    }
    else {
      _tme_rfb_put8(client, 1);
      _tme_rfb_put8(client, (display->tme_rfb_display_password != NULL ? 2 : 1));
      client->tme_rfb_client_state = TME_RFB_STATE_SECURITY;
    }
    return (12);

  case TME_RFB_STATE_SECURITY:
    TME_RFB_TRACE(display, client, "security type %d", p[0]);
    if (p[0] != (display->tme_rfb_display_password != NULL ? 2 : 1)) {
      _tme_rfb_disconnect(display, client, "it wants another security type");
      return (-1);
    }
    if (display->tme_rfb_display_password != NULL) {
      _tme_rfb_send_challenge(client);
    }
    else {
      if (client->tme_rfb_client_minor == 8) {
	_tme_rfb_put32(client, 0);
      }
      client->tme_rfb_client_state = TME_RFB_STATE_CLIENT_INIT;
    }
    return (1);

  case TME_RFB_STATE_AUTH:
    if (n < 16) {
      return (0);
    }
    if (_tme_rfb_password_ok(display, client, p)) {
      TME_RFB_TRACE(display, client, "password ok");
      _tme_rfb_put32(client, 0);
      client->tme_rfb_client_state = TME_RFB_STATE_CLIENT_INIT;
      return (16);
    }
    TME_RFB_TRACE(display, client, "password wrong");
    tme_log(&display->display.tme_display_element->tme_element_log_handle,
	    0, TME_OK,
	    (&display->display.tme_display_element->tme_element_log_handle,
	     _("a VNC client gave the wrong password")));
    _tme_rfb_put32(client, 1);
    if (client->tme_rfb_client_minor == 8) {
      why = "Wrong password";
      _tme_rfb_put32(client, strlen(why));
      _tme_rfb_put(client, why, strlen(why));
    }
    client->tme_rfb_client_closing = TRUE;

    /* (guessing gets slow.) */
    display->tme_rfb_display_wrong_password = TRUE;
    display->tme_rfb_display_wrong_password_at = _tme_display_event_time();
    return (16);

  case TME_RFB_STATE_CLIENT_INIT:
    TME_RFB_TRACE(display, client, "ClientInit: shared %d", p[0]);

    /* not shared: the other clients go: */
    if (p[0] == 0) {
      for (client_i = 0; client_i < TME_RFB_MAX_CLIENTS; client_i++) {
	if (&display->tme_rfb_display_clients[client_i] != client) {
	  _tme_rfb_disconnect(display, &display->tme_rfb_display_clients[client_i], NULL);
	}
      }
    }
    client->tme_rfb_client_state = TME_RFB_STATE_WAIT_SCREEN;
    return (1);

  case TME_RFB_STATE_WAIT_SCREEN:
    /* (nothing more is expected before ServerInit.) */
    return (0);

  default:
    break;
  }

  switch (p[0]) {

  case 0:	/* SetPixelFormat */
    if (n < 20) {
      return (0);
    }
    TME_RFB_TRACE(display, client,
		  "SetPixelFormat: %d bpp, depth %d, %s endian, true colour %d, "
		  "max %lu/%lu/%lu, shift %d/%d/%d",
		  p[4], p[5], (p[6] ? "big" : "little"), p[7],
		  (unsigned long) _tme_rfb_get16(p + 8),
		  (unsigned long) _tme_rfb_get16(p + 10),
		  (unsigned long) _tme_rfb_get16(p + 12),
		  p[14], p[15], p[16]);
    if (p[4] != 8 && p[4] != 16 && p[4] != 32) {
      _tme_rfb_disconnect(display, client, "unsupported pixel format");
      return (-1);
    }
    if (!p[7]) {
      _tme_rfb_disconnect(display, client,
			  "it wants a colour map, which isn't supported");
      return (-1);
    }
    _tme_rfb_pixel_format(client, p[4], p[5], p[6] != 0,
			  _tme_rfb_get16(p + 8), _tme_rfb_get16(p + 10), _tme_rfb_get16(p + 12),
			  p[14] & 31, p[15] & 31, p[16] & 31);
    memset(client->tme_rfb_client_dirty, 2,
	   (size_t) client->tme_rfb_client_tiles_x * client->tme_rfb_client_tiles_y);
    return (20);

  case 2:	/* SetEncodings */
    if (n < 4) {
      return (0);
    }
    len = 4 + 4 * _tme_rfb_get16(p + 2);
    if (n < len) {
      return (0);
    }
    if (display->tme_rfb_display_trace) {
      fprintf(stderr, "vnc[%d]: SetEncodings:",
	      (int) (client - display->tme_rfb_display_clients));
      for (i = 4; i < len; i += 4) {
	fprintf(stderr, " %ld", (long) (tme_int32_t) _tme_rfb_get32(p + i));
      }
      fputc('\n', stderr);
    }

    /* the first encoding there is, in the client's order: */
    client->tme_rfb_client_encoding = TME_RFB_ENC_RAW;
    client->tme_rfb_client_desktop_size = FALSE;
    client->tme_rfb_client_desktop_name = FALSE;
    chosen = FALSE;
    for (i = 4; i < len; i += 4) {
      e = (tme_int32_t) _tme_rfb_get32(p + i);
#ifdef HAVE_ZLIB
      if (!chosen && (e == TME_RFB_ENC_ZRLE || e == TME_RFB_ENC_ZLIB)) {
	client->tme_rfb_client_encoding = e;
      }
#endif /* HAVE_ZLIB */
      if (e == TME_RFB_ENC_RAW || e == TME_RFB_ENC_ZRLE || e == TME_RFB_ENC_ZLIB) {
	chosen = TRUE;
      }
      if (e == TME_RFB_ENC_DESKTOP_SIZE) {
	client->tme_rfb_client_desktop_size = TRUE;
      }
      if (e == TME_RFB_ENC_DESKTOP_NAME) {
	client->tme_rfb_client_desktop_name = TRUE;
      }
    }
    return (len);

  case 3:	/* FramebufferUpdateRequest */
    if (n < 10) {
      return (0);
    }
    TME_RFB_TRACE(display, client, "FramebufferUpdateRequest: %s %lu,%lu %lux%lu",
		  (p[1] ? "incremental" : "full"),
		  (unsigned long) _tme_rfb_get16(p + 2), (unsigned long) _tme_rfb_get16(p + 4),
		  (unsigned long) _tme_rfb_get16(p + 6), (unsigned long) _tme_rfb_get16(p + 8));
    if (!p[1]) {
      /* not incremental: all of the requested area: */
      x = _tme_rfb_get16(p + 2);
      y = _tme_rfb_get16(p + 4);
      w = _tme_rfb_get16(p + 6);
      h = _tme_rfb_get16(p + 8);
      for (ty = y / TME_RFB_TILE;
	   ty < client->tme_rfb_client_tiles_y && ty <= (y + h - 1) / TME_RFB_TILE;
	   ty++) {
	for (tx = x / TME_RFB_TILE;
	     tx < client->tme_rfb_client_tiles_x && tx <= (x + w - 1) / TME_RFB_TILE;
	     tx++) {
	  client->tme_rfb_client_dirty[ty * client->tme_rfb_client_tiles_x + tx] = 2;
	}
      }
    }
    client->tme_rfb_client_update_requested = TRUE;
    return (10);

  case 4:	/* KeyEvent */
    if (n < 8) {
      return (0);
    }
    keysym = _tme_rfb_get32(p + 4);
    TME_RFB_TRACE(display, client, "KeyEvent: %s 0x%lx",
		  (p[1] ? "down" : "up"), (unsigned long) keysym);

    /* Unicode keysyms, for Latin-1: */
    if (keysym >= 0x01000000 && keysym < 0x01000100) {
      keysym -= 0x01000000;
    }
    _tme_keyboard_key_press(p[1] != 0, keysym, &display->display);
    return (8);

  case 5:	/* PointerEvent */
    if (n < 6) {
      return (0);
    }

    /* bits 0..2 are the left, middle and right buttons, as for the
       generic mouse support, which makes the motion from the change
       in the position: */
    _tme_mouse_buttons_event(p[1] & 7, _tme_rfb_get16(p + 2), _tme_rfb_get16(p + 4),
			     &display->display);
    return (6);

  case 6:	/* ClientCutText, which is ignored */
    if (n < 8) {
      return (0);
    }
    client->tme_rfb_client_skip = _tme_rfb_get32(p + 4);
    TME_RFB_TRACE(display, client, "ClientCutText: %lu bytes",
		  (unsigned long) client->tme_rfb_client_skip);
    return (8);

  default:
    snprintf(text, sizeof(text), "unknown message type %d", p[0]);
    _tme_rfb_disconnect(display, client, text);
    return (-1);
  }
}

/* this reads what a client sent: */
static void
_tme_rfb_read_client(struct tme_rfb_display *display,
		     struct tme_rfb_client *client)
{
  tme_uint8_t buf[16384];
  const tme_uint8_t *p;
  ssize_t n;
  size_t done, k;
  long rc;

  while (client->tme_rfb_client_fd >= 0) {
    n = recv(client->tme_rfb_client_fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      if (n == 0
	  || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
	_tme_rfb_disconnect(display, client, NULL);
      }
      return;
    }

    p = buf;
    if (client->tme_rfb_client_skip > 0) {
      k = TME_MIN((size_t) n, client->tme_rfb_client_skip);
      client->tme_rfb_client_skip -= k;
      p += k;
      n -= k;
    }
    if (n == 0) {
      continue;
    }

    if (client->tme_rfb_client_inlen + n > TME_RFB_MAX_MESSAGE) {
      _tme_rfb_disconnect(display, client, "message too long");
      return;
    }
    memcpy(client->tme_rfb_client_in + client->tme_rfb_client_inlen, p, n);
    client->tme_rfb_client_inlen += n;

    done = 0;
    while (client->tme_rfb_client_fd >= 0 && done < client->tme_rfb_client_inlen) {
      if (client->tme_rfb_client_skip > 0) {
	k = TME_MIN(client->tme_rfb_client_inlen - done, client->tme_rfb_client_skip);
	client->tme_rfb_client_skip -= k;
	done += k;
	continue;
      }
      rc = _tme_rfb_client_message(display, client,
				   client->tme_rfb_client_in + done,
				   client->tme_rfb_client_inlen - done);
      if (rc <= 0) {
	break;
      }
      done += rc;
    }
    if (client->tme_rfb_client_fd < 0) {
      return;
    }
    memmove(client->tme_rfb_client_in,
	    client->tme_rfb_client_in + done,
	    client->tme_rfb_client_inlen - done);
    client->tme_rfb_client_inlen -= done;
  }
}

/* this accepts clients: */
static void
_tme_rfb_accept_clients(struct tme_rfb_display *display)
{
  struct tme_rfb_client *client;
  int fd, one, client_i;

  while ((fd = accept(display->tme_rfb_display_listen_fd, NULL, NULL)) >= 0) {
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    one = 1;
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (void *) &one, sizeof(one));
#endif
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *) &one, sizeof(one));

    client = NULL;
    for (client_i = 0; client_i < TME_RFB_MAX_CLIENTS; client_i++) {
      if (display->tme_rfb_display_clients[client_i].tme_rfb_client_fd < 0) {
	client = &display->tme_rfb_display_clients[client_i];
	break;
      }
    }
    if (client == NULL) {
      tme_log(&display->display.tme_display_element->tme_element_log_handle,
	      0, TME_OK,
	      (&display->display.tme_display_element->tme_element_log_handle,
	       _("too many VNC clients")));
      close(fd);
      continue;
    }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    client->tme_rfb_client_fd = fd;
    client->tme_rfb_client_state = TME_RFB_STATE_VERSION;
    client->tme_rfb_client_in = tme_new(tme_uint8_t, TME_RFB_MAX_MESSAGE);
    TME_RFB_TRACE(display, client, "connected");
    _tme_rfb_put(client, "RFB 003.008\n", 12);
  }
}

/* the display update function, called by the display thread between
   screen updates, without the mutex: */
static int
_tme_rfb_display_update(struct tme_display *_display)
{
  struct tme_rfb_display *display;
  struct tme_rfb_client *client;
  struct tme_rfb_screen *screen;
  unsigned int bells;
  int client_i;

  display = (struct tme_rfb_display *) _display;

  /* after a wrong password, the next client waits a second: */
  if (display->tme_rfb_display_wrong_password
      && (_tme_display_event_time()
	  - display->tme_rfb_display_wrong_password_at) >= 1000) {
    display->tme_rfb_display_wrong_password = FALSE;
  }
  if (!display->tme_rfb_display_wrong_password) {
    _tme_rfb_accept_clients(display);
  }

  /* handle what the clients sent: */
  for (client_i = 0; client_i < TME_RFB_MAX_CLIENTS; client_i++) {
    client = &display->tme_rfb_display_clients[client_i];
    if (client->tme_rfb_client_fd >= 0 && !client->tme_rfb_client_closing) {
      _tme_rfb_read_client(display, client);
    }
  }

  /* send the clients any bells, and what they asked for: */
  tme_mutex_lock(&display->display.tme_display_mutex);
  bells = display->tme_rfb_display_bells;
  display->tme_rfb_display_bells = 0;
  screen = _tme_rfb_shown_screen(display);
  for (client_i = 0; client_i < TME_RFB_MAX_CLIENTS; client_i++) {
    client = &display->tme_rfb_display_clients[client_i];
    if (client->tme_rfb_client_fd < 0) {
      continue;
    }
    if (client->tme_rfb_client_state == TME_RFB_STATE_WAIT_SCREEN
	&& screen != NULL) {
      _tme_rfb_server_init(display, client, screen);
    }
    if (client->tme_rfb_client_state != TME_RFB_STATE_NORMAL) {
      continue;
    }
    if (bells > 0) {
      _tme_rfb_put8(client, 2);
    }
    if (client->tme_rfb_client_update_requested
	&& client->tme_rfb_client_outlen == 0) {
      _tme_rfb_send_update(display, client);
    }
  }
  tme_mutex_unlock(&display->display.tme_display_mutex);

  /* send what there is: */
  for (client_i = 0; client_i < TME_RFB_MAX_CLIENTS; client_i++) {
    client = &display->tme_rfb_display_clients[client_i];
    _tme_rfb_flush(display, client);
    if (client->tme_rfb_client_closing
	&& client->tme_rfb_client_outlen == 0) {
      _tme_rfb_disconnect(display, client, NULL);
    }
  }

  return (TME_OK);
}

/* the bell, sent to the clients: */
static int
_tme_rfb_display_bell(struct tme_display *_display)
{
  ((struct tme_rfb_display *) _display)->tme_rfb_display_bells++;
  return (TME_OK);
}

/* this is called with the mutex locked when a screen's framebuffer
   size has changed: */
static int
_tme_rfb_screen_resize(struct tme_screen *_screen)
{
  struct tme_rfb_screen *screen;
  struct tme_fb_connection *conn_fb;
  const struct tme_fb_connection *conn_fb_other;
  int width, height;
  size_t size;

  screen = (struct tme_rfb_screen *) _screen;
  conn_fb = screen->screen.tme_screen_fb;
  conn_fb_other = (const struct tme_fb_connection *) conn_fb->tme_fb_connection.tme_connection_other;

  /* the framebuffer translation writes the width and height that
     _tme_screen_configure picked, which has a scanline more than is
     shown: */
  width = conn_fb->tme_fb_connection_width;
  height = conn_fb->tme_fb_connection_height;

  /* (re)allocate the pixels: */
  size = (size_t) width * height * sizeof(tme_uint32_t);
  if (size != screen->tme_rfb_screen_pixels_size) {
    if (screen->tme_rfb_screen_pixels != NULL) {
      tme_free(screen->tme_rfb_screen_pixels);
    }
    screen->tme_rfb_screen_pixels = tme_malloc0(size);
    screen->tme_rfb_screen_pixels_size = size;
  }
  screen->tme_rfb_screen_stride = width;
  screen->tme_rfb_screen_width = conn_fb_other->tme_fb_connection_width;
  screen->tme_rfb_screen_height = conn_fb_other->tme_fb_connection_height;
  screen->tme_rfb_screen_generation++;

  /* update our framebuffer connection: */
  conn_fb->tme_fb_connection_skipx = 0;
  conn_fb->tme_fb_connection_scanline_pad = _tme_scanline_pad(width * sizeof(tme_uint32_t));
  conn_fb->tme_fb_connection_order = TME_ENDIAN_NATIVE;
  conn_fb->tme_fb_connection_buffer = (tme_uint8_t *) screen->tme_rfb_screen_pixels;
  conn_fb->tme_fb_connection_buffsz = size;
  conn_fb->tme_fb_connection_bits_per_pixel = 32;
  conn_fb->tme_fb_connection_depth = 24;
  conn_fb->tme_fb_connection_class = TME_FB_XLAT_CLASS_COLOR;
  conn_fb->tme_fb_connection_mask_g = 0x00ff00;
  conn_fb->tme_fb_connection_mask_b = 0x0000ff;
  conn_fb->tme_fb_connection_mask_r = 0xff0000;

  /* the translation only writes what changed since it last ran, so
     make the next one start over and draw all of the new pixels: */
  screen->screen.tme_screen_fb_xlat = NULL;
  return (TME_OK);
}

/* this is called with the mutex locked when a screen's framebuffer
   has changed: */
static int
_tme_rfb_screen_redraw(struct tme_screen *_screen, int x, int y, int w, int h)
{
  ((struct tme_rfb_screen *) _screen)->tme_rfb_screen_generation++;
  return (TME_OK);
}

/* this captures an image of a screen, with the mutex locked: */
static int
_tme_rfb_screen_capture(struct tme_screen *_screen,
			struct tme_display_image *image)
{
  struct tme_rfb_screen *screen;
  const tme_uint32_t *row;
  tme_uint32_t *pixel;
  int x, y;

  screen = (struct tme_rfb_screen *) _screen;
  if (screen->tme_rfb_screen_pixels == NULL
      || screen->tme_rfb_screen_width <= 0
      || screen->tme_rfb_screen_height <= 0) {
    return (EAGAIN);
  }
  image->tme_display_image_width = screen->tme_rfb_screen_width;
  image->tme_display_image_height = screen->tme_rfb_screen_height;
  image->tme_display_image_pixels
    = tme_new(tme_uint32_t, (size_t) screen->tme_rfb_screen_width * screen->tme_rfb_screen_height);
  pixel = image->tme_display_image_pixels;
  for (y = 0; y < screen->tme_rfb_screen_height; y++) {
    row = screen->tme_rfb_screen_pixels + (size_t) y * screen->tme_rfb_screen_stride;
    for (x = 0; x < screen->tme_rfb_screen_width; x++) {
      *(pixel++) = row[x] & 0xffffff;
    }
  }
  return (TME_OK);
}

/* this makes a new screen, with the mutex locked: */
static struct tme_screen *
_tme_rfb_screen_new(struct tme_display *display,
		    struct tme_connection *conn)
{
  struct tme_rfb_screen *screen;

  screen = tme_screen_new(display, struct tme_rfb_screen, conn);

  /* clients scale the screen themselves, if they want: */
  screen->screen.tme_screen_fb_scale = TME_FB_XLAT_SCALE_NONE;
  return (&screen->screen);
}

/* this starts listening on a TCP port, returning an errno value: */
static int
_tme_rfb_listen_tcp(struct tme_rfb_display *display,
		    const char *host, const char *port, int *_loopback)
{
  struct addrinfo hints, *ai;
  int fd, one, zero, rc;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  if (getaddrinfo(host, port, &hints, &ai) != 0) {
    return (EHOSTUNREACH);
  }
  fd = socket(ai->ai_family, SOCK_STREAM, 0);
  if (fd < 0) {
    rc = errno;
    freeaddrinfo(ai);
    return (rc);
  }
  one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &one, sizeof(one));

  /* [::] is IPv4's addresses too (not the default on BSDs): */
  if (ai->ai_family == AF_INET6) {
    zero = 0;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (void *) &zero, sizeof(zero));
  }
  if (bind(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
    rc = errno;
    close(fd);
    freeaddrinfo(ai);
    return (rc);
  }
  *_loopback
    = (ai->ai_family == AF_INET6
       ? IN6_IS_ADDR_LOOPBACK(&((struct sockaddr_in6 *) ai->ai_addr)->sin6_addr)
       : (ntohl(((struct sockaddr_in *) ai->ai_addr)->sin_addr.s_addr) >> 24) == 127);
  freeaddrinfo(ai);
  display->tme_rfb_display_listen_fd = fd;
  return (TME_OK);
}

/* this starts listening: on a Unix socket, or on a TCP port of an
   address (this computer only, without one), the first free one from
   5900 up without a port.  it returns an errno value: */
static int
_tme_rfb_listen(struct tme_rfb_display *display,
		const char *address, const char *port, const char *path,
		char **_output)
{
  struct sockaddr_un sun;
  struct stat st;
  char portbuf[16];
  long portnr;
  char *end;
  int loopback, rc, path_i;

  /* a Unix socket: */
  if (path != NULL) {
    if (path[0] == '\0' || strlen(path) >= sizeof(sun.sun_path)) {
      tme_output_append_error(_output, "%s %s", path, _("is a bad socket path"));
      return (EINVAL);
    }

    /* replace an old socket, but nothing else: */
    if (lstat(path, &st) == 0) {
      if (!S_ISSOCK(st.st_mode)) {
	tme_output_append_error(_output, _("%s exists, and isn't a socket"), path);
	return (EEXIST);
      }
      unlink(path);
    }
    display->tme_rfb_display_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (display->tme_rfb_display_listen_fd < 0) {
      rc = errno;
      tme_output_append_error(_output, _("cannot listen on %s: %s"), path, strerror(rc));
      return (rc);
    }
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", path);
    if (bind(display->tme_rfb_display_listen_fd, (struct sockaddr *) &sun, sizeof(sun)) < 0) {
      rc = errno;
      close(display->tme_rfb_display_listen_fd);
      display->tme_rfb_display_listen_fd = -1;
      tme_output_append_error(_output, _("cannot listen on %s: %s"), path, strerror(rc));
      return (rc);
    }
    display->tme_rfb_display_unix_path = tme_strdup(path);
    for (path_i = 0; path_i < (int) TME_ARRAY_ELS(_tme_rfb_unix_paths); path_i++) {
      if (_tme_rfb_unix_paths[path_i] == NULL) {
	if (path_i == 0) {
	  atexit(_tme_rfb_remove_unix_sockets);
	}
	_tme_rfb_unix_paths[path_i] = display->tme_rfb_display_unix_path;
	break;
      }
    }
    fprintf(stderr, "tmesh: VNC server: socket %s\n", path);
  }

  /* a TCP port: */
  else {
    if (address == NULL) {
      address = "127.0.0.1";
    }
    if (port != NULL) {
      portnr = strtol(port, &end, 10);
      if (port[0] == '\0' || *end != '\0' || portnr < 0 || portnr > 65535) {
	tme_output_append_error(_output, "%s %s", port, _("is a bad port"));
	return (EINVAL);
      }
      rc = _tme_rfb_listen_tcp(display, address, port, &loopback);
    }
    else {
      rc = EADDRINUSE;
      for (portnr = TME_RFB_PORT_FIRST; portnr <= TME_RFB_PORT_LAST; portnr++) {
	snprintf(portbuf, sizeof(portbuf), "%ld", portnr);
	rc = _tme_rfb_listen_tcp(display, address, portbuf, &loopback);
	if (rc != EADDRINUSE) {
	  break;
	}
      }
    }
    if (rc != TME_OK) {
      tme_output_append_error(_output, _("cannot listen for VNC clients on %s: %s"),
			      address, strerror(rc));
      return (rc);
    }
    fprintf(stderr, "tmesh: VNC server: %s port %ld\n", address, portnr);
    if (!loopback && display->tme_rfb_display_password == NULL) {
      fprintf(stderr, "tmesh: anyone who can reach this port can use the "
	      "emulator (set TME_VNC_PASSWORD for a password)\n");
    }
  }

  if (listen(display->tme_rfb_display_listen_fd, TME_RFB_MAX_CLIENTS) < 0) {
    rc = errno;
    tme_output_append_error(_output, _("cannot listen: %s"), strerror(rc));
    return (rc);
  }
  fcntl(display->tme_rfb_display_listen_fd, F_SETFD, FD_CLOEXEC);
  fcntl(display->tme_rfb_display_listen_fd, F_SETFL,
	fcntl(display->tme_rfb_display_listen_fd, F_GETFL) | O_NONBLOCK);
  if (display->tme_rfb_display_password != NULL) {
    fprintf(stderr, "tmesh: VNC clients need the password (from TME_VNC_PASSWORD)\n");
  }
  return (TME_OK);
}

/* the new VNC display function: */
TME_ELEMENT_SUB_NEW_DECL(tme_host_rfb,display) {
  struct tme_rfb_display *display;
  const char *address, *port, *path, *password;
  int arg_i, client_i, rc, usage;

  /* the arguments are [ port PORT ] [ address ADDRESS ] or
     [ socket PATH ]: */
  address = NULL;
  port = NULL;
  path = NULL;
  usage = FALSE;
  for (arg_i = 1; args[arg_i] != NULL; arg_i += 2) {
    if (args[arg_i + 1] == NULL) {
      usage = TRUE;
    }
    else if (strcmp(args[arg_i], "port") == 0 && port == NULL) {
      port = args[arg_i + 1];
    }
    else if (strcmp(args[arg_i], "address") == 0 && address == NULL) {
      address = args[arg_i + 1];
    }
    else if (strcmp(args[arg_i], "socket") == 0 && path == NULL) {
      path = args[arg_i + 1];
    }
    else {
      usage = TRUE;
    }
    if (usage) {
      tme_output_append_error(_output, "%s %s, ", args[arg_i], _("unexpected"));
      break;
    }
  }
  if (path != NULL && (port != NULL || address != NULL)) {
    tme_output_append_error(_output, "socket %s, ", _("unexpected with a port or address"));
    usage = TRUE;
  }
  if (usage) {
    tme_output_append_error(_output, "%s %s [ port %s ] [ address %s ] | [ socket %s ]",
			    _("usage:"), args[0],
			    _("PORT"), _("ADDRESS"), _("PATH"));
    return (EINVAL);
  }

  /* start our data structure: */
  display = tme_new0(struct tme_rfb_display, 1);
  display->tme_rfb_display_listen_fd = -1;
  for (client_i = 0; client_i < TME_RFB_MAX_CLIENTS; client_i++) {
    display->tme_rfb_display_clients[client_i].tme_rfb_client_fd = -1;
  }
  display->tme_rfb_display_trace = (getenv("TME_VNC_DEBUG") != NULL);
  password = getenv("TME_VNC_PASSWORD");
  if (password != NULL && password[0] != '\0') {
    display->tme_rfb_display_password = tme_strdup(password);
    if (strlen(password) > 8) {
      fprintf(stderr, "tmesh: only the first 8 characters of TME_VNC_PASSWORD "
	      "count (VNC passwords are at most 8)\n");
    }
  }

  rc = _tme_rfb_listen(display, address, port, path, _output);
  if (rc != TME_OK) {
    if (display->tme_rfb_display_password != NULL) {
      tme_free(display->tme_rfb_display_password);
    }
    tme_free(display);
    return (rc);
  }

  tme_display_init(element, &display->display);

  /* set the display-specific functions: */
  display->display.tme_display_bell = _tme_rfb_display_bell;
  display->display.tme_display_update = _tme_rfb_display_update;
  display->display.tme_screen_add = _tme_rfb_screen_new;
  display->display.tme_screen_resize = _tme_rfb_screen_resize;
  display->display.tme_screen_redraw = _tme_rfb_screen_redraw;
  display->display.tme_screen_capture = _tme_rfb_screen_capture;

  return (TME_OK);
}
