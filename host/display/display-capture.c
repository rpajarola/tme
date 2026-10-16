/* host/display/display-capture.c - screenshots and recordings: */

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

/* any display backend that can capture an image of a screen gets
   screenshots, as PNG files, and recordings, as MP4 files made by
   ffmpeg, from here, along with the display element's screenshot and
   record commands.  images are at the size of the emulated
   framebuffer, however the screen is scaled: */

#include <tme/common.h>

/* includes: */
#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif /* HAVE_ZLIB */

/* recordings have this many frames per second: */
#define TME_DISPLAY_RECORD_FPS	(10)

/* a recording: */
struct tme_display_record {

  /* the pipe to ffmpeg: */
  FILE *tme_display_record_pipe;

  /* the file being recorded: */
  char *tme_display_record_file;

  /* the frame size: */
  int tme_display_record_width;
  int tme_display_record_height;

  /* when the recording started, in milliseconds, and how many frames
     have been sent: */
  tme_uint32_t tme_display_record_start;
  unsigned long tme_display_record_frames;
};

/* this shows a message about a capture: */
static void
_tme_display_capture_message(struct tme_screen *screen,
			     const char *format,
			     const char *name)
{
  _tme_display_message(screen, format, name);
}

/* this captures an image of a screen, with the display mutex locked,
   returning an errno value: */
static int
_tme_display_capture(struct tme_screen *screen,
		     struct tme_display_image *image,
		     char **_output)
{
  struct tme_display *display;
  int rc;

  display = screen->tme_screen_display;
  if (display->tme_screen_capture == NULL) {
    tme_output_append_error(_output, _("this display can't capture its screen"));
    return (ENOTSUP);
  }
  memset(image, 0, sizeof(*image));
  rc = (*display->tme_screen_capture)(screen, image);
  if (rc != TME_OK) {
    tme_output_append_error(_output, _("there is no screen to capture yet"));
  }
  return (rc);
}

/* this returns a new file name for a screen capture, in the current
   directory, from the date and time: */
static char *
_tme_display_capture_name(const char *extension)
{
  char name[64];
  char stamp[32];
  time_t now;
  unsigned int suffix;

  now = time(NULL);
  strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
  snprintf(name, sizeof(name), "tme-%s.%s", stamp, extension);
  for (suffix = 2; access(name, F_OK) == 0; suffix++) {
    snprintf(name, sizeof(name), "tme-%s-%u.%s", stamp, suffix, extension);
  }
  return (tme_strdup(name));
}

/* the CRC-32 that PNG and zlib use: */
static tme_uint32_t
_tme_display_png_crc(tme_uint32_t crc, const tme_uint8_t *data, size_t size)
{
  static tme_uint32_t table[256];
  tme_uint32_t c;
  unsigned int n, k;

  if (table[1] == 0) {
    for (n = 0; n < 256; n++) {
      c = n;
      for (k = 0; k < 8; k++) {
	c = (c & 1) ? 0xedb88320 ^ (c >> 1) : c >> 1;
      }
      table[n] = c;
    }
  }
  crc = ~crc;
  for (; size > 0; size--) {
    crc = table[(crc ^ *(data++)) & 0xff] ^ (crc >> 8);
  }
  return (~crc);
}

/* this writes a big-endian 32-bit value: */
static void
_tme_display_png_be32(tme_uint8_t *bytes, tme_uint32_t value)
{
  bytes[0] = value >> 24;
  bytes[1] = value >> 16;
  bytes[2] = value >> 8;
  bytes[3] = value;
}

/* this writes a PNG chunk: */
static int
_tme_display_png_chunk(FILE *file, const char *type,
		       const tme_uint8_t *data, size_t size)
{
  tme_uint8_t header[8], trailer[4];
  tme_uint32_t crc;

  _tme_display_png_be32(header, size);
  memcpy(header + 4, type, 4);
  crc = _tme_display_png_crc(0, header + 4, 4);
  crc = _tme_display_png_crc(crc, data, size);
  _tme_display_png_be32(trailer, crc);
  return (fwrite(header, sizeof(header), 1, file) == 1
	  && (size == 0 || fwrite(data, size, 1, file) == 1)
	  && fwrite(trailer, sizeof(trailer), 1, file) == 1);
}

/* this compresses data into a zlib stream, returning it and setting
   its size.  without zlib, the stream has the data uncompressed: */
static tme_uint8_t *
_tme_display_png_deflate(const tme_uint8_t *data, size_t size, size_t *_size)
{
  tme_uint8_t *stream;
#ifdef HAVE_ZLIB
  uLongf stream_size;

  stream_size = compressBound(size);
  stream = tme_new(tme_uint8_t, stream_size);
  if (compress2(stream, &stream_size, data, size, Z_BEST_COMPRESSION) != Z_OK) {
    tme_free(stream);
    return (NULL);
  }
  *_size = stream_size;
  return (stream);
#else  /* !HAVE_ZLIB */
  size_t block, offset, out;
  tme_uint32_t a, b;
  size_t i;

  stream = tme_new(tme_uint8_t, 2 + size + 5 * (size / 65535 + 1) + 4);
  out = 0;
  stream[out++] = 0x78;
  stream[out++] = 0x01;
  offset = 0;
  do {
    block = TME_MIN(size - offset, 65535);
    stream[out++] = (offset + block == size);
    stream[out++] = block & 0xff;
    stream[out++] = block >> 8;
    stream[out++] = ~block & 0xff;
    stream[out++] = (~block >> 8) & 0xff;
    memcpy(stream + out, data + offset, block);
    out += block;
    offset += block;
  } while (offset < size);
  a = 1;
  b = 0;
  for (i = 0; i < size; i++) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  _tme_display_png_be32(stream + out, (b << 16) | a);
  out += 4;
  *_size = out;
  return (stream);
#endif /* !HAVE_ZLIB */
}

/* this writes an image as a PNG file, returning an errno value: */
static int
_tme_display_png_write(const char *name, const struct tme_display_image *image)
{
  static const tme_uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
  tme_uint8_t ihdr[13];
  tme_uint8_t *raw, *row, *stream;
  const tme_uint32_t *pixel;
  size_t raw_size, stream_size;
  FILE *file;
  int x, y, ok, rc;

  /* each row is a filter type byte, of no filtering, and then the
     pixels as RGB: */
  raw_size = (size_t) image->tme_display_image_height * (1 + 3 * (size_t) image->tme_display_image_width);
  raw = tme_new(tme_uint8_t, raw_size);
  row = raw;
  pixel = image->tme_display_image_pixels;
  for (y = 0; y < image->tme_display_image_height; y++) {
    *(row++) = 0;
    for (x = 0; x < image->tme_display_image_width; x++, pixel++) {
      *(row++) = *pixel >> 16;
      *(row++) = *pixel >> 8;
      *(row++) = *pixel;
    }
  }
  stream = _tme_display_png_deflate(raw, raw_size, &stream_size);
  tme_free(raw);
  if (stream == NULL) {
    return (ENOMEM);
  }

  file = fopen(name, "wb");
  if (file == NULL) {
    rc = errno;
    tme_free(stream);
    return (rc);
  }
  _tme_display_png_be32(ihdr + 0, image->tme_display_image_width);
  _tme_display_png_be32(ihdr + 4, image->tme_display_image_height);
  ihdr[8] = 8;		/* bit depth */
  ihdr[9] = 2;		/* RGB */
  ihdr[10] = 0;		/* deflate */
  ihdr[11] = 0;		/* adaptive filtering */
  ihdr[12] = 0;		/* no interlace */
  ok = (fwrite(signature, sizeof(signature), 1, file) == 1
	&& _tme_display_png_chunk(file, "IHDR", ihdr, sizeof(ihdr))
	&& _tme_display_png_chunk(file, "IDAT", stream, stream_size)
	&& _tme_display_png_chunk(file, "IEND", NULL, 0));
  rc = (ok ? TME_OK : errno);
  if (fclose(file) != 0 && ok) {
    ok = FALSE;
    rc = errno;
  }
  tme_free(stream);
  if (!ok) {
    remove(name);
    return (rc != TME_OK ? rc : EIO);
  }
  return (TME_OK);
}

/* this saves a screenshot as a PNG file, with the display mutex
   locked, returning an errno value: */
static int
_tme_display_screenshot_locked(struct tme_screen *screen,
			       const char *file,
			       char **_output)
{
  struct tme_display_image image;
  char *name;
  int rc;

  rc = _tme_display_capture(screen, &image, _output);
  if (rc != TME_OK) {
    return (rc);
  }
  name = (file != NULL ? tme_strdup(file) : _tme_display_capture_name("png"));
  rc = _tme_display_png_write(name, &image);
  tme_free(image.tme_display_image_pixels);
  if (rc != TME_OK) {
    tme_output_append_error(_output, "%s: %s", name, strerror(rc));
    tme_free(name);
    return (rc);
  }
  _tme_display_capture_message(screen, _("Saved screenshot %s"), name);
  tme_output_append(_output, "%s", name);
  tme_free(name);
  return (TME_OK);
}

/* this saves a screenshot as a PNG file, returning an errno value: */
int
_tme_display_screenshot(struct tme_screen *screen,
			const char *file,
			char **_output)
{
  struct tme_display *display;
  int rc;

  display = screen->tme_screen_display;
  tme_mutex_lock(&display->tme_display_mutex);
  rc = _tme_display_screenshot_locked(screen, file, _output);
  tme_mutex_unlock(&display->tme_display_mutex);
  return (rc);
}

/* this ends a recording, returning nonzero if ffmpeg finished the file
   successfully: */
static int
_tme_display_record_end(struct tme_screen *screen)
{
  struct tme_display_record *record;
  int ok;

  record = screen->tme_screen_record;
  screen->tme_screen_record = NULL;
  ok = (record->tme_display_record_pipe != NULL
	&& pclose(record->tme_display_record_pipe) == 0);
  _tme_display_capture_message(screen,
			       (ok
				? _("Saved recording %s")
				: _("Recording %s failed")),
			       record->tme_display_record_file);
  tme_free(record->tme_display_record_file);
  tme_free(record);
  _tme_display_menu_changed(screen);
  return (ok);
}

/* this sends the current screen to ffmpeg as however many frames of a
   recording are due by now, so the recording plays back in real time,
   and stops the recording if that fails.  the display mutex must be
   locked: */
void
_tme_display_record_tick(struct tme_screen *screen)
{
  struct tme_display_record *record;
  struct tme_display_image image;
  unsigned long frames_due, count;
  size_t row_size;
  char *output;
  int ok;

  record = screen->tme_screen_record;
  if (record == NULL) {
    return;
  }

  /* see how many frames are due.  after a long stall, like the host
     sleeping, skip most of the frames that were missed: */
  frames_due
    = (((unsigned long) (_tme_display_event_time() - record->tme_display_record_start)
	* TME_DISPLAY_RECORD_FPS
	/ 1000)
       + 1);
  if (frames_due <= record->tme_display_record_frames) {
    return;
  }
  count = TME_MIN(frames_due - record->tme_display_record_frames,
		  5 * TME_DISPLAY_RECORD_FPS);
  record->tme_display_record_frames = frames_due;

  /* send the frames: */
  output = NULL;
  ok = (_tme_display_capture(screen, &image, &output) == TME_OK);
  if (output != NULL) {
    tme_free(output);
  }
  if (ok) {
    ok = (image.tme_display_image_width == record->tme_display_record_width
	  && image.tme_display_image_height == record->tme_display_record_height);
    row_size = sizeof(tme_uint32_t) * image.tme_display_image_width;
    for (; ok && count > 0; count--) {
      ok = (fwrite(image.tme_display_image_pixels,
		   row_size,
		   image.tme_display_image_height,
		   record->tme_display_record_pipe)
	    == (size_t) image.tme_display_image_height);
    }
    ok = ok && fflush(record->tme_display_record_pipe) == 0;
    tme_free(image.tme_display_image_pixels);
  }
  if (!ok) {
    _tme_display_record_end(screen);
    _tme_display_message(screen, _("Recording stopped: the screen changed size, or ffmpeg failed"));
  }
}

/* this starts recording the screen into an MP4 file with ffmpeg, with
   the display mutex locked, returning an errno value: */
static int
_tme_display_record_start_locked(struct tme_screen *screen,
				 const char *file,
				 char **_output)
{
  struct tme_display_record *record;
  struct tme_display_image image;
  char *name;
  char *command;
  int rc;

  if (screen->tme_screen_record != NULL) {
    tme_output_append_error(_output, _("already recording %s"),
			    screen->tme_screen_record->tme_display_record_file);
    return (EBUSY);
  }
  rc = _tme_display_capture(screen, &image, _output);
  if (rc != TME_OK) {
    return (rc);
  }
  tme_free(image.tme_display_image_pixels);
  if (system("command -v ffmpeg >/dev/null 2>&1") != 0) {
    tme_output_append_error(_output, _("recording needs ffmpeg, which wasn't found"));
    _tme_display_message(screen, _("Recording needs ffmpeg, which wasn't found"));
    return (ENOENT);
  }
  name = (file != NULL ? tme_strdup(file) : _tme_display_capture_name("mp4"));
  if (strchr(name, '\'') != NULL) {
    tme_output_append_error(_output, _("%s: file names with quotes are not supported"), name);
    tme_free(name);
    return (EINVAL);
  }

  /* start ffmpeg.  the pixels are 0x00RRGGBB in host byte order.  an
     odd frame size is padded to an even one, which the H.264 encoder
     needs: */
  record = tme_new0(struct tme_display_record, 1);
  record->tme_display_record_width = image.tme_display_image_width;
  record->tme_display_record_height = image.tme_display_image_height;
  command = tme_new(char, strlen(name) + 512);
  sprintf(command,
	  "ffmpeg -loglevel error -y"
	  " -f rawvideo -pixel_format %s -video_size %dx%d -framerate %d -i -"
	  " -vf 'pad=ceil(iw/2)*2:ceil(ih/2)*2'"
	  " -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p"
	  " '%s'",
	  (TME_ENDIAN_NATIVE == TME_ENDIAN_LITTLE ? "bgr0" : "0rgb"),
	  record->tme_display_record_width,
	  record->tme_display_record_height,
	  TME_DISPLAY_RECORD_FPS,
	  name);
  signal(SIGPIPE, SIG_IGN);
  record->tme_display_record_pipe = popen(command, "w");
  tme_free(command);
  if (record->tme_display_record_pipe == NULL) {
    rc = errno;
    tme_output_append_error(_output, _("couldn't start ffmpeg: %s"), strerror(rc));
    tme_free(record);
    tme_free(name);
    return (rc);
  }
  record->tme_display_record_file = name;
  screen->tme_screen_record = record;

  /* send the first frame now: */
  record->tme_display_record_start = _tme_display_event_time();
  record->tme_display_record_frames = 0;
  _tme_display_record_tick(screen);
  if (screen->tme_screen_record == NULL) {
    tme_output_append_error(_output, _("ffmpeg failed"));
    return (EIO);
  }

  _tme_display_capture_message(screen, _("Recording to %s"), name);
  tme_output_append(_output, "%s", name);
  _tme_display_menu_changed(screen);
  return (TME_OK);
}

/* this starts recording the screen into an MP4 file with ffmpeg,
   returning an errno value: */
int
_tme_display_record_start(struct tme_screen *screen,
			  const char *file,
			  char **_output)
{
  struct tme_display *display;
  int rc;

  display = screen->tme_screen_display;
  tme_mutex_lock(&display->tme_display_mutex);
  rc = _tme_display_record_start_locked(screen, file, _output);
  tme_mutex_unlock(&display->tme_display_mutex);
  return (rc);
}

/* this stops recording the screen, returning an errno value: */
int
_tme_display_record_stop(struct tme_screen *screen, char **_output)
{
  struct tme_display *display;
  char *name;
  int rc;

  display = screen->tme_screen_display;
  tme_mutex_lock(&display->tme_display_mutex);
  if (screen->tme_screen_record == NULL) {
    tme_output_append_error(_output, _("not recording"));
    rc = ENOENT;
  }
  else {

    /* send any frames due by now, and wait for ffmpeg to finish the
       file: */
    _tme_display_record_tick(screen);
    rc = EIO;
    if (screen->tme_screen_record != NULL) {
      name = tme_strdup(screen->tme_screen_record->tme_display_record_file);
      if (_tme_display_record_end(screen)) {
	tme_output_append(_output, "%s", name);
	rc = TME_OK;
      }
      tme_free(name);
    }
    if (rc != TME_OK) {
      tme_output_append_error(_output, _("ffmpeg failed"));
    }
  }
  tme_mutex_unlock(&display->tme_display_mutex);
  return (rc);
}

/* this returns nonzero if a screen is being recorded: */
int
_tme_display_recording(struct tme_screen *screen)
{
  return (screen->tme_screen_record != NULL);
}

/* the display element command function: */
int
_tme_display_command(struct tme_element *element,
		     const char * const *args,
		     char **_output)
{
  struct tme_display *display;
  struct tme_screen *screen;

  display = (struct tme_display *) element->tme_element_private;
  screen = display->tme_display_screens;

  /* screenshot [ FILE ]: */
  if (TME_ARG_IS(args[1], "screenshot")
      && (args[2] == NULL || args[3] == NULL)) {
    if (screen == NULL) {
      tme_output_append_error(_output, _("there is no screen"));
      return (ENOENT);
    }
    return (_tme_display_screenshot(screen, args[2], _output));
  }

  /* record [ FILE | stop ]: */
  if (TME_ARG_IS(args[1], "record")
      && (args[2] == NULL || args[3] == NULL)) {
    if (screen == NULL) {
      tme_output_append_error(_output, _("there is no screen"));
      return (ENOENT);
    }
    if (TME_ARG_IS(args[2], "stop")) {
      return (_tme_display_record_stop(screen, _output));
    }
    return (_tme_display_record_start(screen, args[2], _output));
  }

  if (args[1] == NULL) {
    tme_output_append_error(_output, _("missing command"));
  }
  else {
    tme_output_append_error(_output, "%s '%s', ", _("unknown command"), args[1]);
  }
  tme_output_append_error(_output,
			  _("available %s commands: %s"),
			  args[0],
			  "screenshot [ FILE ], record [ FILE | stop ]");
  return (EINVAL);
}
