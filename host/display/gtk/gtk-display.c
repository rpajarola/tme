/* $Id: gtk-screen.c,v 1.11 2009/08/30 21:39:03 fredette Exp $ */

/* host/gtk/gtk-screen.c - GTK screen support: */

/*
 * Copyright (c) 2003 Matt Fredette
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

#include <tme/common.h>
_TME_RCSID("$Id: gtk-screen.c,v 1.11 2009/08/30 21:39:03 fredette Exp $");

/* includes: */
#include "gtk-display.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

static void _tme_gtk_screen_record_tick _TME_P((struct tme_gtk_screen *));

static void
_tme_gtk_display_bell(struct tme_gdk_display *display) {
  gdk_display_beep(display->tme_gdk_display);
}

static int
_tme_gtk_display_update(struct tme_display *display) {
  struct tme_screen *screen;

  while (gtk_events_pending ())
    gtk_main_iteration ();

  /* update the media menus and status, if the media have changed: */
  for (screen = display->tme_display_screens;
       screen != NULL;
       screen = screen->tme_screen_next) {
    _tme_gtk_media_update((struct tme_gtk_screen *) screen);

    /* if the screen is being recorded, send any frames that are due: */
    _tme_gtk_screen_record_tick((struct tme_gtk_screen *) screen);
  }
  return TME_OK;
}

/* this is called when the quit confirmation is answered: */
static void
_tme_gtk_screen_quit_response(GtkDialog *dialog,
			      gint response,
			      gpointer unused)
{
  gtk_widget_destroy(GTK_WIDGET(dialog));
  if (response == GTK_RESPONSE_ACCEPT) {
    exit(0);
  }
}

/* this asks whether to quit the emulator: */
static void
_tme_gtk_screen_quit(struct tme_gtk_screen *screen)
{
  GtkWidget *dialog;

  dialog
    = gtk_message_dialog_new(GTK_WINDOW(screen->tme_gtk_screen_window),
			     GTK_DIALOG_DESTROY_WITH_PARENT,
			     GTK_MESSAGE_QUESTION,
			     GTK_BUTTONS_NONE,
			     _("Quit the emulator?"));
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
					   _("The emulated machine will stop immediately."));
  gtk_dialog_add_buttons(GTK_DIALOG(dialog),
			 _("_Cancel"), GTK_RESPONSE_CANCEL,
			 _("_Quit"), GTK_RESPONSE_ACCEPT,
			 NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

  /* the dialog isn't modal, since running it would stop the
     emulator: */
  g_signal_connect(dialog, "response",
		   G_CALLBACK(_tme_gtk_screen_quit_response), NULL);
  gtk_widget_show(dialog);
}

/* this is called for the Quit menu item: */
static void
_tme_gtk_screen_quit_item(GtkWidget *menu_item,
			  struct tme_gtk_screen *screen)
{
  _tme_gtk_screen_quit(screen);
}

/* this is called when the window is being closed: */
static gboolean
_tme_gtk_screen_delete(GtkWidget *window,
		       GdkEvent *event,
		       struct tme_gtk_screen *screen)
{
  /* closing the window quits the emulator, after asking: */
  _tme_gtk_screen_quit(screen);
  return (TRUE);
}

/* this sets the screen scaling to that indicated by the Scale menu: */
static void
_tme_gtk_screen_scale_default(GtkWidget *widget,
			      struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_scale_set(screen, -TME_FB_XLAT_SCALE_NONE );
}

/* this sets the screen scaling to that indicated by the Scale menu: */
static void
_tme_gtk_screen_scale_half(GtkWidget *widget,
			   struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_scale_set(screen, TME_FB_XLAT_SCALE_HALF );
}

/* this sets the screen scaling to that indicated by the Scale menu: */
static void
_tme_gtk_screen_scale_full(GtkWidget *widget,
			   struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_scale_set(screen, TME_FB_XLAT_SCALE_NONE );
}

/* this sets the screen scaling to that indicated by the Scale menu: */
static void
_tme_gtk_screen_scale_double(GtkWidget *widget,
			     struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_scale_set(screen, TME_FB_XLAT_SCALE_DOUBLE );
}

/* this sets the screen size: */
static inline void
_tme_screen_format_set(struct tme_gtk_screen *screen,
		       cairo_format_t format)
{
  screen->tme_gtk_screen_format = format;
  _tme_screen_configure(screen);
}

/* this sets the screen format to that indicated by the Format menu: */
static void
_tme_gtk_screen_format_argb32(GtkWidget *widget,
			      struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_format_set(screen, CAIRO_FORMAT_ARGB32);
}

/* this sets the screen format to that indicated by the Format menu: */
static void
_tme_gtk_screen_format_rgb24(GtkWidget *widget,
			     struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_format_set(screen, CAIRO_FORMAT_RGB24);
}

/* this sets the screen format to that indicated by the Format menu: */
static void
_tme_gtk_screen_format_a8(GtkWidget *widget,
			  struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_format_set(screen, CAIRO_FORMAT_A8);
}

/* this sets the screen format to that indicated by the Format menu: */
static void
_tme_gtk_screen_format_a1(GtkWidget *widget,
			  struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_format_set(screen, CAIRO_FORMAT_A1);
}

/* this sets the screen format to that indicated by the Format menu: */
static void
_tme_gtk_screen_format_rgb16_565(GtkWidget *widget,
				 struct tme_gtk_screen *screen)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(GTK_MENU_ITEM(widget))))
    return;

  _tme_screen_format_set(screen, CAIRO_FORMAT_RGB16_565);
}

static struct tme_display_menu_item scale_items[] =
  {
   { _("Default"), G_CALLBACK(_tme_gtk_screen_scale_default) },
   { _("Half"), G_CALLBACK(_tme_gtk_screen_scale_half) },
   { _("Full"), G_CALLBACK(_tme_gtk_screen_scale_full) },
   { _("Double"), G_CALLBACK(_tme_gtk_screen_scale_double) }
  };

static struct tme_display_menu_item format_items[] =
  {
   { _("ARGB32"), G_CALLBACK(_tme_gtk_screen_format_argb32) },
   { _("RGB24"), G_CALLBACK(_tme_gtk_screen_format_rgb24) },
   { _("A8"), G_CALLBACK(_tme_gtk_screen_format_a8) },
   { _("A1"), G_CALLBACK(_tme_gtk_screen_format_a1) },
   { _("RGB16_565"), G_CALLBACK(_tme_gtk_screen_format_rgb16_565) }
  };

/* Screen-specific size request */
static void _tme_gtk_screen_resize(struct tme_gtk_screen *screen) {
  struct tme_fb_connection *conn_fb = screen->screen.tme_screen_fb;
  int scale_factor = screen->screen.tme_screen_scale;
  
  /* set a minimum size.  the framebuffer is in device pixels, and the
     size request is in logical ones: */
  gtk_widget_set_size_request(screen->tme_gtk_screen_gtkframe,
			      ((conn_fb->tme_fb_connection_width
				+ scale_factor - 1)
			       / scale_factor),
			      ((conn_fb->tme_fb_connection_height
				+ scale_factor - 1)
			       / scale_factor));
}

/* Create a similar image surface to the screen's target surface (i.e., backing store) */
static inline cairo_surface_t *
_tme_gtk_screen_create_similar_image(GdkWindow *window,
				     cairo_format_t format,
				     int width,
				     int height) 
{
  cairo_surface_t *surface;

#if GTK_MINOR_VERSION < 10
  surface = 
    cairo_image_surface_create(format,
			       width,
			       height);
#else
  surface = 
    gdk_window_create_similar_image_surface(window,
					    format,
					    width,
					    height,
					    0);
#endif
    return surface;
}

/* Create a new surface of the appropriate size to store our scribbles */
static void
_tme_gtk_screen_init(GtkWidget         *widget,
		     struct tme_gtk_screen *screen)
{
  tme_uint8_t *bitmap_data;
  int bitmap_width, bitmap_height;
  unsigned int y;

  screen->tme_gtk_screen_surface = 
    _tme_gtk_screen_create_similar_image(gtk_widget_get_window(widget),
					 CAIRO_FORMAT_A1,
					 gtk_widget_get_allocated_width(widget),
					 gtk_widget_get_allocated_height(widget));

  /* create an image surface of an alternating-bits area. */
  bitmap_data = cairo_image_surface_get_data(screen->tme_gtk_screen_surface);
  assert(bitmap_data != NULL);
  bitmap_width = cairo_image_surface_get_width(screen->tme_gtk_screen_surface) / 8;
  bitmap_height = cairo_image_surface_get_height(screen->tme_gtk_screen_surface);
  cairo_surface_flush(screen->tme_gtk_screen_surface);
  for (y = 0;
       y < bitmap_height;
       y++) {
    memset(bitmap_data + y * bitmap_width,
	   (y & 1
	    ? 0x33
	    : 0xcc),
	   bitmap_width);
  }
  cairo_surface_mark_dirty(screen->tme_gtk_screen_surface);
}

/* Create a new surface of the appropriate size to store our scribbles */
static gboolean
_tme_gtk_screen_configure(GtkWidget         *widget,
			  GdkEventConfigure *event,
			  gpointer          _screen)
{
  struct tme_gtk_screen *screen;
  struct tme_display *display;
  struct tme_fb_connection *conn_fb;
  GdkWindow *window;
  int scale;
  int width, height;
  cairo_surface_t *surface;
  double device_scale_x, device_scale_y;
  
  screen = (struct tme_gtk_screen *) _screen;

  /* get the display: */
  display = screen->screen.tme_screen_display;

  /* lock our mutex: */
  tme_mutex_lock(&display->tme_display_mutex);

  window = gtk_widget_get_window(screen->tme_gtk_screen_gtkframe);
  
  screen->screen.tme_screen_scale = gdk_window_get_scale_factor(window);

  conn_fb = screen->screen.tme_screen_fb;

  /* the framebuffer translation writes scanlines as wide as the
     framebuffer size that _tme_screen_configure picked, in device
     pixels, so make the surface exactly that size, rather than the
     window's (which is in logical pixels, and on a HiDPI display is
     only a fraction of that).  until that size is known, use the
     window's: */
  width = conn_fb->tme_fb_connection_width;
  height = conn_fb->tme_fb_connection_height;
  if (width <= 0 || height <= 0) {
    width = gdk_window_get_width(window) * screen->screen.tme_screen_scale;
    height = gdk_window_get_height(window) * screen->screen.tme_screen_scale;
  }

  /* keep the current surface if it's still right, since a new one
     starts out blank: */
  surface = screen->tme_gtk_screen_surface;
  if (surface == NULL
      || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE
      || cairo_image_surface_get_width(surface) != width
      || cairo_image_surface_get_height(surface) != height
      || cairo_image_surface_get_format(surface) != screen->tme_gtk_screen_format
      || (cairo_surface_get_device_scale(surface, &device_scale_x, &device_scale_y),
	  device_scale_x != screen->screen.tme_screen_scale)) {

    if (surface != NULL) {
      cairo_surface_destroy(surface);
    }
    screen->tme_gtk_screen_surface
      = gdk_window_create_similar_image_surface(window,
						screen->tme_gtk_screen_format,
						width,
						height,
						screen->screen.tme_screen_scale);

    /* the translation only writes what changed since it last ran, so
       make the next one start over and draw the whole new surface: */
    screen->screen.tme_screen_fb_xlat = NULL;
  }

  /* update our framebuffer connection: */
  conn_fb->tme_fb_connection_width = cairo_image_surface_get_width(screen->tme_gtk_screen_surface);
  conn_fb->tme_fb_connection_height = cairo_image_surface_get_height(screen->tme_gtk_screen_surface);
  conn_fb->tme_fb_connection_skipx = 0;
  conn_fb->tme_fb_connection_scanline_pad = _tme_scanline_pad(cairo_image_surface_get_stride(screen->tme_gtk_screen_surface));
  conn_fb->tme_fb_connection_order = TME_ENDIAN_NATIVE;
  conn_fb->tme_fb_connection_buffer = cairo_image_surface_get_data(screen->tme_gtk_screen_surface);
  conn_fb->tme_fb_connection_buffsz = cairo_image_surface_get_stride(screen->tme_gtk_screen_surface) * conn_fb->tme_fb_connection_height;
  conn_fb->tme_fb_connection_bits_per_pixel = 32;
  conn_fb->tme_fb_connection_depth = 24;
  conn_fb->tme_fb_connection_class = TME_FB_XLAT_CLASS_COLOR;
  conn_fb->tme_fb_connection_mask_g = 0x00ff00;
  conn_fb->tme_fb_connection_mask_b = 0x0000ff;
  conn_fb->tme_fb_connection_mask_r = 0xff0000;

  /* unlock our mutex: */
  tme_mutex_unlock(&display->tme_display_mutex);

  /* We've handled the configure event, no need for further processing. */
  return TRUE;
}

/* this removes a screen capture message from the status bar: */
static gboolean
_tme_gtk_screen_message_pop(gpointer _screen)
{
  struct tme_gtk_screen *screen;

  screen = (struct tme_gtk_screen *) _screen;
  gtk_statusbar_pop(GTK_STATUSBAR(screen->tme_gtk_screen_mouse_statusbar),
		    screen->tme_gtk_screen_capture_cid);
  return (G_SOURCE_REMOVE);
}

/* this shows a screen capture message in the status bar for a few
   seconds: */
static void
_tme_gtk_screen_message(struct tme_gtk_screen *screen, const char *message)
{
  if (screen->tme_gtk_screen_mouse_statusbar == NULL) {
    return;
  }
  gtk_statusbar_push(GTK_STATUSBAR(screen->tme_gtk_screen_mouse_statusbar),
		     screen->tme_gtk_screen_capture_cid,
		     message);
  g_timeout_add_seconds(5, _tme_gtk_screen_message_pop, screen);
}

/* this returns a new image of the screen, at the size of the
   emulated framebuffer, or NULL if there is no screen yet: */
static cairo_surface_t *
_tme_gtk_screen_capture(struct tme_gtk_screen *screen)
{
  cairo_surface_t *source;
  cairo_surface_t *image;
  const struct tme_fb_connection *conn_fb_other;
  double device_scale_x, device_scale_y;
  double width_logical, height_logical;
  int width, height;
  cairo_t *cr;

  /* get the screen surface: */
  source = screen->tme_gtk_screen_surface;
  if (source == NULL
      || cairo_surface_get_type(source) != CAIRO_SURFACE_TYPE_IMAGE) {
    return (NULL);
  }
  cairo_surface_flush(source);
  cairo_surface_get_device_scale(source, &device_scale_x, &device_scale_y);
  width_logical = cairo_image_surface_get_width(source) / device_scale_x;
  height_logical = cairo_image_surface_get_height(source) / device_scale_y;

  /* the image is the size of the emulated framebuffer, which the
     screen surface may be scaled from: */
  conn_fb_other
    = (screen->screen.tme_screen_fb != NULL
       ? ((const struct tme_fb_connection *)
	  screen->screen.tme_screen_fb->tme_fb_connection.tme_connection_other)
       : NULL);
  if (conn_fb_other != NULL
      && conn_fb_other->tme_fb_connection_width > 0
      && conn_fb_other->tme_fb_connection_height > 0) {
    width = conn_fb_other->tme_fb_connection_width;
    height = conn_fb_other->tme_fb_connection_height;
  }
  else {
    width = width_logical;
    height = height_logical;
  }

  /* draw the screen surface into the image, without smoothing: */
  image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
  cr = cairo_create(image);
  cairo_scale(cr, width / width_logical, height / height_logical);
  cairo_set_source_surface(cr, source, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(image);
  return (image);
}

/* this returns a new file name for a screen capture, in the current
   directory, from the date and time: */
static char *
_tme_gtk_screen_capture_name(const char *extension)
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

/* this saves a screenshot as a PNG file, returning an errno value: */
static int
_tme_gtk_screen_screenshot(struct tme_gtk_screen *screen,
			   const char *file,
			   char **_output)
{
  cairo_surface_t *image;
  cairo_status_t status;
  char *name;
  char message[256];

  image = _tme_gtk_screen_capture(screen);
  if (image == NULL) {
    tme_output_append_error(_output, _("there is no screen to capture yet"));
    return (EAGAIN);
  }
  name = (file != NULL ? tme_strdup(file) : _tme_gtk_screen_capture_name("png"));
  status = cairo_surface_write_to_png(image, name);
  cairo_surface_destroy(image);
  if (status != CAIRO_STATUS_SUCCESS) {
    tme_output_append_error(_output, "%s: %s", name, cairo_status_to_string(status));
    tme_free(name);
    return (EIO);
  }
  snprintf(message, sizeof(message), _("Saved screenshot %s"), name);
  _tme_gtk_screen_message(screen, message);
  tme_output_append(_output, "%s", name);
  tme_free(name);
  return (TME_OK);
}

/* recordings have this many frames per second: */
#define TME_GTK_RECORD_FPS	(10)

/* this sends the current screen to ffmpeg as however many frames of
   a recording are due by now, so the recording plays back in real
   time, and stops the recording if that fails: */
static void _tme_gtk_screen_record_stop _TME_P((struct tme_gtk_screen *, char **));
static void
_tme_gtk_screen_record_tick(struct tme_gtk_screen *screen)
{
  cairo_surface_t *image;
  unsigned long frames_due, count;
  int ok;

  if (screen->tme_gtk_screen_record == NULL) {
    return;
  }

  /* see how many frames are due.  after a long stall, like the host
     sleeping, skip most of the frames that were missed: */
  frames_due
    = (((g_get_monotonic_time() - screen->tme_gtk_screen_record_start)
	* TME_GTK_RECORD_FPS
	/ G_USEC_PER_SEC)
       + 1);
  if (frames_due <= screen->tme_gtk_screen_record_frames) {
    return;
  }
  count = TME_MIN(frames_due - screen->tme_gtk_screen_record_frames,
		  5 * TME_GTK_RECORD_FPS);
  screen->tme_gtk_screen_record_frames = frames_due;

  /* send the frames: */
  image = _tme_gtk_screen_capture(screen);
  ok = (image != NULL
	&& cairo_image_surface_get_width(image) == screen->tme_gtk_screen_record_width
	&& cairo_image_surface_get_height(image) == screen->tme_gtk_screen_record_height
	&& cairo_image_surface_get_stride(image) == screen->tme_gtk_screen_record_width * 4);
  for (; ok && count > 0; count--) {
    ok = (fwrite(cairo_image_surface_get_data(image),
		 cairo_image_surface_get_stride(image),
		 screen->tme_gtk_screen_record_height,
		 screen->tme_gtk_screen_record)
	  == (size_t) screen->tme_gtk_screen_record_height);
  }
  ok = ok && fflush(screen->tme_gtk_screen_record) == 0;
  if (image != NULL) {
    cairo_surface_destroy(image);
  }
  if (!ok) {
    _tme_gtk_screen_record_stop(screen, NULL);
    _tme_gtk_screen_message(screen, _("Recording stopped: the screen changed size, or ffmpeg failed"));
  }
}

/* this starts recording the screen into an MP4 file with ffmpeg,
   returning an errno value: */
static int
_tme_gtk_screen_record_start(struct tme_gtk_screen *screen,
			     const char *file,
			     char **_output)
{
  cairo_surface_t *image;
  char *name;
  char *command;
  char message[256];

  if (screen->tme_gtk_screen_record != NULL) {
    tme_output_append_error(_output, _("already recording %s"), screen->tme_gtk_screen_record_file);
    return (EBUSY);
  }
  if (system("command -v ffmpeg >/dev/null 2>&1") != 0) {
    tme_output_append_error(_output, _("recording needs ffmpeg, which wasn't found"));
    _tme_gtk_screen_message(screen, _("Recording needs ffmpeg, which wasn't found"));
    return (ENOENT);
  }
  image = _tme_gtk_screen_capture(screen);
  if (image == NULL) {
    tme_output_append_error(_output, _("there is no screen to capture yet"));
    return (EAGAIN);
  }
  name = (file != NULL ? tme_strdup(file) : _tme_gtk_screen_capture_name("mp4"));
  if (strchr(name, '\'') != NULL) {
    tme_output_append_error(_output, _("%s: file names with quotes are not supported"), name);
    cairo_surface_destroy(image);
    tme_free(name);
    return (EINVAL);
  }

  /* start ffmpeg.  an odd frame size is padded to an even one, which
     the H.264 encoder needs: */
  screen->tme_gtk_screen_record_width = cairo_image_surface_get_width(image);
  screen->tme_gtk_screen_record_height = cairo_image_surface_get_height(image);
  cairo_surface_destroy(image);
  command = tme_new(char, strlen(name) + 512);
  sprintf(command,
	  "ffmpeg -loglevel error -y"
	  " -f rawvideo -pixel_format bgr0 -video_size %dx%d -framerate %d -i -"
	  " -vf 'pad=ceil(iw/2)*2:ceil(ih/2)*2'"
	  " -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p"
	  " '%s'",
	  screen->tme_gtk_screen_record_width,
	  screen->tme_gtk_screen_record_height,
	  TME_GTK_RECORD_FPS,
	  name);
  signal(SIGPIPE, SIG_IGN);
  screen->tme_gtk_screen_record = popen(command, "w");
  tme_free(command);
  if (screen->tme_gtk_screen_record == NULL) {
    tme_output_append_error(_output, _("couldn't start ffmpeg: %s"), strerror(errno));
    tme_free(name);
    return (errno);
  }
  screen->tme_gtk_screen_record_file = name;

  /* send the first frame now: */
  screen->tme_gtk_screen_record_start = g_get_monotonic_time();
  screen->tme_gtk_screen_record_frames = 0;
  _tme_gtk_screen_record_tick(screen);
  if (screen->tme_gtk_screen_record == NULL) {
    tme_output_append_error(_output, _("ffmpeg failed"));
    return (EIO);
  }

  snprintf(message, sizeof(message), _("Recording to %s"), name);
  _tme_gtk_screen_message(screen, message);
  tme_output_append(_output, "%s", name);
  if (screen->tme_gtk_screen_record_item != NULL) {
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(screen->tme_gtk_screen_record_item), TRUE);
  }
  return (TME_OK);
}

/* this stops recording the screen: */
static void
_tme_gtk_screen_record_stop(struct tme_gtk_screen *screen, char **_output)
{
  FILE *record;
  char message[256];
  int status;

  record = screen->tme_gtk_screen_record;
  if (record == NULL) {
    return;
  }

  /* send any frames due by now, and wait for ffmpeg to finish the
     file: */
  _tme_gtk_screen_record_tick(screen);
  record = screen->tme_gtk_screen_record;
  screen->tme_gtk_screen_record = NULL;
  status = (record != NULL ? pclose(record) : -1);
  if (status == 0) {
    snprintf(message, sizeof(message), _("Saved recording %s"), screen->tme_gtk_screen_record_file);
    if (_output != NULL) {
      tme_output_append(_output, "%s", screen->tme_gtk_screen_record_file);
    }
  }
  else {
    snprintf(message, sizeof(message), _("Recording %s failed"), screen->tme_gtk_screen_record_file);
  }
  _tme_gtk_screen_message(screen, message);
  tme_free(screen->tme_gtk_screen_record_file);
  screen->tme_gtk_screen_record_file = NULL;
  if (screen->tme_gtk_screen_record_item != NULL) {
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(screen->tme_gtk_screen_record_item), FALSE);
  }
}

/* the Save Screenshot menu item: */
static void
_tme_gtk_screen_screenshot_item(GtkWidget *menu_item, struct tme_gtk_screen *screen)
{
  struct tme_display *display;
  char *output;

  display = screen->screen.tme_screen_display;
  output = NULL;
  tme_mutex_lock(&display->tme_display_mutex);
  if (_tme_gtk_screen_screenshot(screen, NULL, &output) != TME_OK
      && output != NULL) {
    _tme_gtk_screen_message(screen, output);
  }
  tme_mutex_unlock(&display->tme_display_mutex);
  if (output != NULL) {
    tme_free(output);
  }
}

/* the Record menu item: */
static void
_tme_gtk_screen_record_item(GtkWidget *menu_item, struct tme_gtk_screen *screen)
{
  struct tme_display *display;
  char *output;
  int active;

  display = screen->screen.tme_screen_display;
  active = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menu_item));
  output = NULL;
  tme_mutex_lock(&display->tme_display_mutex);

  /* this item is also set when a command starts or stops a recording,
     so only act if the state really changed: */
  if (active && screen->tme_gtk_screen_record == NULL) {
    if (_tme_gtk_screen_record_start(screen, NULL, &output) != TME_OK) {
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item), FALSE);
    }
  }
  else if (!active && screen->tme_gtk_screen_record != NULL) {
    _tme_gtk_screen_record_stop(screen, &output);
  }
  tme_mutex_unlock(&display->tme_display_mutex);
  if (output != NULL) {
    tme_free(output);
  }
}

/* this is called before the screen's display is updated: */
static void
_tme_gtk_screen_redraw(struct tme_gtk_screen *screen, int x, int y, int w, int h)
{
  cairo_surface_flush(screen->tme_gtk_screen_surface);
  cairo_surface_mark_dirty(screen->tme_gtk_screen_surface);
  gtk_widget_queue_draw(screen->tme_gtk_screen_gtkframe);
}

/* the GTK display command function: */
static int
_tme_gtk_display_command(struct tme_element *element, const char * const *args, char **_output)
{
  struct tme_display *display;
  struct tme_gtk_screen *screen;
  int rc;

  display = (struct tme_display *) element->tme_element_private;
  screen = (struct tme_gtk_screen *) display->tme_display_screens;

  /* screenshot [ FILE ]: */
  if (TME_ARG_IS(args[1], "screenshot")
      && (args[2] == NULL || args[3] == NULL)) {
    if (screen == NULL) {
      tme_output_append_error(_output, _("there is no screen"));
      return (ENOENT);
    }
    tme_mutex_lock(&display->tme_display_mutex);
    rc = _tme_gtk_screen_screenshot(screen, args[2], _output);
    tme_mutex_unlock(&display->tme_display_mutex);
    return (rc);
  }

  /* record [ FILE | stop ]: */
  if (TME_ARG_IS(args[1], "record")
      && (args[2] == NULL || args[3] == NULL)) {
    if (screen == NULL) {
      tme_output_append_error(_output, _("there is no screen"));
      return (ENOENT);
    }
    tme_mutex_lock(&display->tme_display_mutex);
    if (TME_ARG_IS(args[2], "stop")) {
      if (screen->tme_gtk_screen_record == NULL) {
	tme_output_append_error(_output, _("not recording"));
	rc = ENOENT;
      }
      else {
	_tme_gtk_screen_record_stop(screen, _output);
	rc = TME_OK;
      }
    }
    else {
      rc = _tme_gtk_screen_record_start(screen, args[2], _output);
    }
    tme_mutex_unlock(&display->tme_display_mutex);
    return (rc);
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

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
_tme_gtk_screen_draw(GtkWidget *widget,
		     cairo_t   *cr,
		     gpointer   _screen)
{
  struct tme_display *display;
  struct tme_gtk_screen *screen;

  screen = (struct tme_gtk_screen *) _screen;

  /* get the display: */
  display = screen->screen.tme_screen_display;

  /* lock our mutex: */
  tme_mutex_lock(&display->tme_display_mutex);

  cairo_set_source_surface(cr, screen->tme_gtk_screen_surface, 0, 0);
  cairo_paint(cr);

  /* unlock our mutex: */
  tme_mutex_unlock(&display->tme_display_mutex);

  return FALSE;
}

/* this makes a new screen: */
struct tme_gtk_screen *
_tme_gtk_screen_new(struct tme_gdk_display *display,
		    struct tme_connection *conn)
{
  struct tme_gtk_screen *screen;
  GdkDisplay *gdkdisplay;
  GdkDeviceManager *devices;
  GtkWidget *menu_bar;
  GtkWidget *menu;
  GtkWidget *submenu;
  GtkWidget *menu_item;
  GtkAccelGroup *accel_group;
  guint accel_key;
  GdkModifierType accel_mods;

  screen = tme_screen_new(display, struct tme_gtk_screen, conn);

  screen->screen.tme_screen_scale = gdk_monitor_get_scale_factor(display->tme_gdk_display_monitor);
  
  /* create the top-level window, and allow it to shrink, grow,
     and auto-shrink: */
  screen->tme_gtk_screen_window
    = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_resizable(GTK_WINDOW(screen->tme_gtk_screen_window), FALSE);

  /* create the outer vertical packing box: */
  screen->tme_gtk_screen_vbox0
    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  /* add the outer vertical packing box to the window: */
  gtk_container_add(GTK_CONTAINER(screen->tme_gtk_screen_window),
		    screen->tme_gtk_screen_vbox0);

  /* create the menu bar and pack it into the outer vertical packing
     box: */
  menu_bar = gtk_menu_bar_new ();
  gtk_box_pack_start(GTK_BOX(screen->tme_gtk_screen_vbox0), 
		     menu_bar,
		     FALSE, FALSE, 0);
  //  gtk_widget_show(menu_bar);

  /* create the Screen menu: */
  menu = gtk_menu_new();

  /* create the Screen scaling submenu: */
  submenu = _tme_display_menu_radio(screen, scale_items, TME_ARRAY_ELS(scale_items));

  /* create the Screen scaling submenu item: */
  menu_item = gtk_menu_item_new_with_label(_("Scale"));
  //  gtk_widget_show(menu_item);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

  /* create the Screen colormap submenu: */
  submenu = _tme_display_menu_radio(screen, format_items, TME_ARRAY_ELS(format_items));

  /* create the Screen scaling submenu item: */
  menu_item = gtk_menu_item_new_with_label(_("Format"));
  //  gtk_widget_show(menu_item);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

  /* create the screen capture items: */
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
  menu_item = gtk_menu_item_new_with_label(_("Save Screenshot"));
  g_signal_connect(menu_item, "activate",
		   G_CALLBACK(_tme_gtk_screen_screenshot_item), screen);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
  menu_item = gtk_check_menu_item_new_with_label(_("Record"));
  g_signal_connect(menu_item, "toggled",
		   G_CALLBACK(_tme_gtk_screen_record_item), screen);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
  screen->tme_gtk_screen_record_item = menu_item;

  /* create the Quit item, with Cmd-Q (Ctrl-Q other than on macOS): */
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
  accel_group = gtk_accel_group_new();
  gtk_window_add_accel_group(GTK_WINDOW(screen->tme_gtk_screen_window), accel_group);
  menu_item = gtk_menu_item_new_with_mnemonic(_("_Quit"));
  gtk_accelerator_parse("<Primary>q", &accel_key, &accel_mods);
  gtk_widget_add_accelerator(menu_item, "activate", accel_group,
			     accel_key, accel_mods, GTK_ACCEL_VISIBLE);
  g_signal_connect(menu_item, "activate",
		   G_CALLBACK(_tme_gtk_screen_quit_item), screen);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

  /* closing the window asks whether to quit: */
  g_signal_connect(screen->tme_gtk_screen_window, "delete-event",
		   G_CALLBACK(_tme_gtk_screen_delete), screen);

  /* create the Screen menu bar item, attach the menu to it, and 
     attach the menu bar item to the menu bar: */
  menu_item = gtk_menu_item_new_with_label("Screen");
  //  gtk_widget_show(menu_item);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), menu_item);

  /* create the Media menu, and the media status: */
  _tme_gtk_media_attach(screen, menu_bar);

  /* create the Gtkframe for the framebuffer area: */
  screen->tme_gtk_screen_gtkframe = gtk_drawing_area_new();
  
  /* new a minimum size */
  //_tme_gtk_screen_set_size(screen, BLANK_SIDE, BLANK_SIDE);
  //  _tme_gtk_screen_init(screen->tme_gtk_screen_gtkframe, screen);

  
  _tme_screen_format_set(screen, CAIRO_FORMAT_RGB24);

  /* pack the Gtkframe into the outer vertical packing box: */
  gtk_box_pack_start(GTK_BOX(screen->tme_gtk_screen_vbox0), 
		     screen->tme_gtk_screen_gtkframe,
		     FALSE, FALSE, 0);

  g_signal_connect(screen->tme_gtk_screen_gtkframe, "draw",
		   G_CALLBACK(_tme_gtk_screen_draw), screen);

  g_signal_connect(screen->tme_gtk_screen_gtkframe, "configure-event",
		   G_CALLBACK(_tme_gtk_screen_configure), screen);

  /* attach the mouse to this screen: */
  _tme_gtk_mouse_attach(screen);

  /* screen capture messages go in the mouse status bar: */
  screen->tme_gtk_screen_capture_cid
    = gtk_statusbar_get_context_id(GTK_STATUSBAR(screen->tme_gtk_screen_mouse_statusbar),
				   "screen capture");

  /* attach the keyboard to this screen: */
  _tme_gtk_keyboard_attach(screen);

  gtk_window_set_title(GTK_WINDOW(screen->tme_gtk_screen_window), display->display.tme_display_title);

  /* unlock our mutex: */
  tme_mutex_unlock(&display->display.tme_display_mutex);

  /* show the top-level window: */
  gtk_widget_show_all(screen->tme_gtk_screen_window);

  /* lock our mutex: */
  tme_mutex_lock(&display->display.tme_display_mutex);

  return (screen);
}

static void _tme_gtk_init(void) {
  char **argv;
  char *argv_buffer[3];
  int argc;

  /* GTK requires program to be running non-setuid */
#ifdef HAVE_SETUID
  setuid(getuid());
#endif
  
  /* conjure up an argv.  this is pretty bad: */
  argv = argv_buffer;
  argc = 0;
  argv[argc++] = "tmesh";
#if 1
  argv[argc++] = "--gtk-debug=signals";
#endif
  argv[argc] = NULL;
  gtk_init(&argc, &argv);
}

/* this is a GTK callback for an enter notify event, that has the
   widget grab focus and then continue normal event processing: */
gint
_tme_display_enter_focus(GtkWidget *widget,
			 GdkEvent *gdk_event_raw,
			 gpointer junk)
{

  /* grab the focus: */
  gtk_widget_grab_focus(widget);

  /* continue normal event processing: */
  return (FALSE);
}

/* this creates a menu of radio buttons: */
GtkWidget *
_tme_display_menu_radio(struct tme_gtk_screen *screen,
			struct tme_display_menu_item menu_items[],
			int num_items)
{
  GtkWidget *menu;
  GSList *menu_group;
  GtkWidget *menu_item;
  int i;
  
  /* create the menu: */
  menu = gtk_menu_new();

  /* create the menu items: */
  menu_group = NULL;
  for (i=0;i<num_items;i++) {
    menu_item
      = gtk_radio_menu_item_new_with_label(menu_group,
					   menu_items[i].name);
    menu_group
      = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    g_signal_connect(menu_item, 
		     "activate",
		     menu_items[i].menu_func,
		     (gpointer) screen);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    //    gtk_widget_show(menu_item);
  }

  /* return the menu: */
  return (menu);
}

/* the new GTK display function: */
TME_ELEMENT_SUB_NEW_DECL(tme_host_gtk,display) {
  struct tme_gdk_display *display;
  GdkRectangle workarea;

  /* start our data structure: */
  display = tme_new0(struct tme_gdk_display, 1);
  tme_display_init(element, display);

  /* recover our data structure: */
  display = element->tme_element_private;

  _tme_gtk_init();
  
  display->tme_gdk_display = gdk_display_get_default();

  display->tme_gdk_display_cursor
    = gdk_cursor_new_for_display(display->tme_gdk_display, GDK_BLANK_CURSOR);

  display->tme_gdk_display_seat = gdk_display_get_default_seat(display->tme_gdk_display);

  display->tme_gdk_display_monitor = gdk_display_get_primary_monitor(display->tme_gdk_display);

  gdk_monitor_get_workarea(display->tme_gdk_display_monitor, &workarea);

  if(GDK_IS_MONITOR(display->tme_gdk_display_monitor) &&
     workarea.width &&
     workarea.height) {
    display->display.tme_screen_width = workarea.width;    
    display->display.tme_screen_height = workarea.height;
  }

  /* set the display-specific functions: */
  display->display.tme_display_bell = _tme_gtk_display_bell;
  display->display.tme_display_update = _tme_gtk_display_update;
  display->display.tme_screen_add = _tme_gtk_screen_new;
  display->display.tme_screen_resize = _tme_gtk_screen_resize;
  display->display.tme_screen_redraw = _tme_gtk_screen_redraw;

  /* the screen capture commands: */
  element->tme_element_command = _tme_gtk_display_command;

  /* setup the thread loop function: */
  //tme_threads_init(NULL, gtk_main);

  /* unlock mutex once gtk main thread is running: */
  //  g_idle_add(_tme_gtk_screens_update, display);

  return (TME_OK);
}
