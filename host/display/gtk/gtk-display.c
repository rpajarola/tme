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


static void
_tme_gtk_display_bell(struct tme_gdk_display *display) {
  gdk_display_beep(display->tme_gdk_display);
}

static int
_tme_gtk_display_update(struct tme_display *display) {
  struct tme_screen *screen;

  /* bring the menus up to date before handling events, since the
     menu items refer to the shared menus as they are now: */
  for (screen = display->tme_display_screens;
       screen != NULL;
       screen = screen->tme_screen_next) {
    _tme_gtk_menu_update((struct tme_gtk_screen *) screen);
  }

  while (gtk_events_pending ())
    gtk_main_iteration ();

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
_tme_gtk_screen_quit(struct tme_screen *_screen)
{
  struct tme_gtk_screen *screen;
  GtkWidget *dialog;

  screen = (struct tme_gtk_screen *) _screen;
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

/* this is called when the window is being closed: */
static gboolean
_tme_gtk_screen_delete(GtkWidget *window,
		       GdkEvent *event,
		       struct tme_gtk_screen *screen)
{
  /* closing the window quits the emulator, after asking: */
  _tme_gtk_screen_quit(&screen->screen);
  return (TRUE);
}

/* this sets the screen format: */
static inline void
_tme_screen_format_set(struct tme_gtk_screen *screen,
		       cairo_format_t format)
{
  screen->tme_gtk_screen_format = format;
  _tme_screen_configure(screen);
}

/* this is called for a Format item: */
static void
_tme_gtk_screen_format_item(struct tme_screen *screen,
			    struct tme_display_menu_item *item)
{
  _tme_screen_format_set((struct tme_gtk_screen *) screen,
			 (cairo_format_t) item->tme_display_menu_item_value);
}

/* this adds the Format submenu, after the Scale submenu, to a
   screen's new shared menus: */
void
_tme_gtk_screen_menus_new(struct tme_gtk_screen *screen)
{
  static const struct {
    const char *label;
    cairo_format_t format;
  } formats[] = {
    { "ARGB32", CAIRO_FORMAT_ARGB32 },
    { "RGB24", CAIRO_FORMAT_RGB24 },
    { "A8", CAIRO_FORMAT_A8 },
    { "A1", CAIRO_FORMAT_A1 },
    { "RGB16_565", CAIRO_FORMAT_RGB16_565 },
  };
  struct tme_display_menus *menus;
  struct tme_display_menu *screen_menu;
  struct tme_display_menu_item *scale, *item;
  unsigned int format_i;

  menus = screen->screen.tme_screen_menus;
  screen_menu = menus->tme_display_menus_bar->tme_display_menu_items->tme_display_menu_item_submenu;
  for (scale = screen_menu->tme_display_menu_items;
       scale->tme_display_menu_item_submenu != menus->tme_display_menus_scale;
       scale = scale->tme_display_menu_item_next);
  item = _tme_display_menu_insert(screen_menu, scale,
				  TME_DISPLAY_MENU_ITEM_SUBMENU,
				  _("Format"), NULL, NULL);
  screen_menu = item->tme_display_menu_item_submenu;
  for (format_i = 0; format_i < TME_ARRAY_ELS(formats); format_i++) {
    item = _tme_display_menu_append(screen_menu,
				    TME_DISPLAY_MENU_ITEM_RADIO,
				    formats[format_i].label,
				    _tme_gtk_screen_format_item, NULL);
    item->tme_display_menu_item_value = formats[format_i].format;
    item->tme_display_menu_item_checked
      = (formats[format_i].format == screen->tme_gtk_screen_format);
  }
  _tme_display_menu_changed(&screen->screen);
}

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
_tme_gtk_screen_message(struct tme_screen *_screen, const char *message)
{
  struct tme_gtk_screen *screen;

  screen = (struct tme_gtk_screen *) _screen;
  if (screen->tme_gtk_screen_mouse_statusbar == NULL) {
    return;
  }
  gtk_statusbar_push(GTK_STATUSBAR(screen->tme_gtk_screen_mouse_statusbar),
		     screen->tme_gtk_screen_capture_cid,
		     message);
  g_timeout_add_seconds(5, _tme_gtk_screen_message_pop, screen);
}

/* this captures an image of the screen, at the size of the emulated
   framebuffer, with the mutex locked: */
static int
_tme_gtk_screen_capture(struct tme_screen *_screen,
			struct tme_display_image *image)
{
  struct tme_gtk_screen *screen;
  cairo_surface_t *source;
  cairo_surface_t *surface;
  const struct tme_fb_connection *conn_fb_other;
  double device_scale_x, device_scale_y;
  double width_logical, height_logical;
  int width, height, y;
  cairo_t *cr;

  /* get the screen surface: */
  screen = (struct tme_gtk_screen *) _screen;
  source = screen->tme_gtk_screen_surface;
  if (source == NULL
      || cairo_surface_get_type(source) != CAIRO_SURFACE_TYPE_IMAGE) {
    return (EAGAIN);
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

  /* draw the screen surface into the image, without smoothing.  an
     RGB24 cairo image has the same pixels as a display image: */
  surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
  cr = cairo_create(surface);
  cairo_scale(cr, width / width_logical, height / height_logical);
  cairo_set_source_surface(cr, source, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(surface);
  image->tme_display_image_width = width;
  image->tme_display_image_height = height;
  image->tme_display_image_pixels = tme_new(tme_uint32_t, (size_t) width * height);
  for (y = 0; y < height; y++) {
    memcpy(image->tme_display_image_pixels + (size_t) y * width,
	   cairo_image_surface_get_data(surface) + (size_t) y * cairo_image_surface_get_stride(surface),
	   width * sizeof(tme_uint32_t));
  }
  cairo_surface_destroy(surface);
  return (TME_OK);
}

/* this is called before the screen's display is updated: */
static void
_tme_gtk_screen_redraw(struct tme_gtk_screen *screen, int x, int y, int w, int h)
{
  cairo_surface_flush(screen->tme_gtk_screen_surface);
  cairo_surface_mark_dirty(screen->tme_gtk_screen_surface);
  gtk_widget_queue_draw(screen->tme_gtk_screen_gtkframe);
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

  /* create the menu bar and the media status: */
  _tme_gtk_menu_attach(screen);

  /* closing the window asks whether to quit: */
  g_signal_connect(screen->tme_gtk_screen_window, "delete-event",
		   G_CALLBACK(_tme_gtk_screen_delete), screen);

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

  display->display.tme_screen_quit = _tme_gtk_screen_quit;
  display->display.tme_screen_message = _tme_gtk_screen_message;
  display->display.tme_screen_capture = _tme_gtk_screen_capture;
  _tme_gtk_menu_init(display);

  /* setup the thread loop function: */
  //tme_threads_init(NULL, gtk_main);

  /* unlock mutex once gtk main thread is running: */
  //  g_idle_add(_tme_gtk_screens_update, display);

  return (TME_OK);
}
