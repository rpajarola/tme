/* $Id: gtk-mouse.c,v 1.3 2007/03/03 15:33:22 fredette Exp $ */

/* host/gtk/gtk-mouse.c - GTK mouse support: */

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
_TME_RCSID("$Id: gtk-mouse.c,v 1.3 2007/03/03 15:33:22 fredette Exp $");

/* includes: */
#include "gtk-display.h"
#include <gdk/gdkkeysyms.h>

#ifdef GDK_WINDOWING_QUARTZ
/* this is from CoreGraphics.  it's declared here, instead of
   including ApplicationServices.h, whose names clash with ours: */
extern int CGAssociateMouseAndMouseCursorPosition(int);
#endif

/* the modifier keys that can make up the key combination that turns
   mouse mode off: */
#define TME_GTK_MOUSE_MOD_CTRL		TME_BIT(0)
#define TME_GTK_MOUSE_MOD_SHIFT		TME_BIT(1)
#define TME_GTK_MOUSE_MOD_ALT		TME_BIT(2)
#define TME_GTK_MOUSE_MOD_CMD		TME_BIT(3)

/* the key combination that turns mouse mode off.  Ctrl+Shift with
   either Alt (Option) or Cmd (Meta, Super) works everywhere, but
   suggest the usual one for the platform: */
#ifdef GDK_WINDOWING_QUARTZ
#define TME_GTK_MOUSE_OFF_KEYS		"Ctrl+Shift+Cmd"
#else
#define TME_GTK_MOUSE_OFF_KEYS		"Ctrl+Shift+Alt"
#endif

/* this sets the mouse label and statusbar: */
static void
_tme_gtk_mouse_status(struct tme_gtk_screen *screen)
{
  int on;

  on = screen->tme_gtk_screen_mouse_on;
  gtk_label_set_text(GTK_LABEL(screen->tme_gtk_screen_mouse_label),
		     (on
		      ? _("Mouse is on")
		      : _("Mouse is off")));
  gtk_statusbar_pop(GTK_STATUSBAR(screen->tme_gtk_screen_mouse_statusbar),
		    screen->tme_gtk_screen_mouse_statusbar_cid);
  gtk_statusbar_push(GTK_STATUSBAR(screen->tme_gtk_screen_mouse_statusbar),
		     screen->tme_gtk_screen_mouse_statusbar_cid,
		     (on
		      ? _("Press " TME_GTK_MOUSE_OFF_KEYS " to release the mouse")
		      : _("Click in the screen to use the mouse")));
}

/* this warps the pointer to the middle of the screen, and makes that
   the position that the next motion is measured from: */
static void
_tme_gtk_mouse_recenter(struct tme_gtk_screen *screen)
{
  struct tme_gdk_display *display;
  GdkWindow *window;
  gint x, y;

  display = (struct tme_gdk_display *) screen->screen.tme_screen_display;
  window = gtk_widget_get_window(screen->tme_gtk_screen_gtkframe);

  gdk_window_get_origin(window, &x, &y);
  x += gdk_window_get_width(window) / 2;
  y += gdk_window_get_height(window) / 2;
  gdk_device_warp(gdk_seat_get_pointer(display->tme_gdk_display_seat),
		  gdk_window_get_screen(window),
		  x, y);
#ifdef GDK_WINDOWING_QUARTZ
  /* otherwise macOS ignores mouse movement for a moment after the
     pointer is warped: */
  CGAssociateMouseAndMouseCursorPosition(TRUE);
#endif

  tme_mutex_lock(&display->display.tme_display_mutex);
  display->display.tme_screen_mouse_warp_x = x;
  display->display.tme_screen_mouse_warp_y = y;
  tme_mutex_unlock(&display->display.tme_display_mutex);
}

/* this turns mouse mode on: */
static void
_tme_gtk_mouse_mode_on(struct tme_gtk_screen *screen,
		       GdkEvent *gdk_event_raw)
{
  struct tme_gdk_display *display;
  GdkWindow *window;
  GdkGrabStatus status;

  display = (struct tme_gdk_display *) screen->screen.tme_screen_display;
  window = gtk_widget_get_window(screen->tme_gtk_screen_gtkframe);

  /* capture the pointer, and hide it: */
  gtk_grab_add(screen->tme_gtk_screen_gtkframe);
  status
    = gdk_seat_grab(display->tme_gdk_display_seat,
		    window,
		    GDK_SEAT_CAPABILITY_ALL_POINTING,
		    TRUE,
		    display->tme_gdk_display_cursor,
		    gdk_event_raw,
		    NULL,
		    NULL);
  if (status != GDK_GRAB_SUCCESS) {
    gtk_grab_remove(screen->tme_gtk_screen_gtkframe);
    return;
  }
  gdk_window_set_cursor(window, display->tme_gdk_display_cursor);

  /* we are now in mouse mode: */
  screen->tme_gtk_screen_mouse_on = TRUE;
  _tme_gtk_mouse_status(screen);

  /* start in the middle of the screen: */
  _tme_gtk_mouse_recenter(screen);
}

/* this turns mouse mode off: */
void
_tme_gtk_mouse_mode_off(struct tme_gtk_screen *screen,
			guint32 time)
{
  struct tme_gdk_display *display;

  if (!screen->tme_gtk_screen_mouse_on) {
    return;
  }

  display = (struct tme_gdk_display *) screen->screen.tme_screen_display;

  /* show and release the pointer: */
  gdk_window_set_cursor(gtk_widget_get_window(screen->tme_gtk_screen_gtkframe),
			NULL);
  gdk_seat_ungrab(display->tme_gdk_display_seat);
  gtk_grab_remove(screen->tme_gtk_screen_gtkframe);

  /* the mouse is now off: */
  screen->tme_gtk_screen_mouse_on = FALSE;
  _tme_gtk_mouse_status(screen);
}

/* this is called for every key press and release, to turn mouse
   mode off when the key combination for that is pressed.  the keys
   are still passed on to the emulated keyboard: */
void
_tme_gtk_mouse_key_event(struct tme_gtk_screen *screen,
			 GdkEventKey *gdk_event)
{
  unsigned int mod;
  unsigned int mods;

  switch (gdk_event->keyval) {
  case GDK_KEY_Control_L:
  case GDK_KEY_Control_R:
    mod = TME_GTK_MOUSE_MOD_CTRL;
    break;
  case GDK_KEY_Shift_L:
  case GDK_KEY_Shift_R:
    mod = TME_GTK_MOUSE_MOD_SHIFT;
    break;
  case GDK_KEY_Alt_L:
  case GDK_KEY_Alt_R:
    mod = TME_GTK_MOUSE_MOD_ALT;
    break;
  case GDK_KEY_Meta_L:
  case GDK_KEY_Meta_R:
  case GDK_KEY_Super_L:
  case GDK_KEY_Super_R:
    mod = TME_GTK_MOUSE_MOD_CMD;
    break;
  default:
    return;
  }

  mods = screen->tme_gtk_screen_mouse_mods;
  if (gdk_event->type == GDK_KEY_PRESS) {
    mods |= mod;
  }
  else {
    mods &= ~mod;
  }
  screen->tme_gtk_screen_mouse_mods = mods;

  if (gdk_event->type == GDK_KEY_PRESS
      && (mods & TME_GTK_MOUSE_MOD_CTRL)
      && (mods & TME_GTK_MOUSE_MOD_SHIFT)
      && (mods & (TME_GTK_MOUSE_MOD_ALT
		  | TME_GTK_MOUSE_MOD_CMD))) {
    _tme_gtk_mouse_mode_off(screen, gdk_event->time);
  }
}

/* this is a GTK callback for the window losing the focus: */
static int
_tme_gtk_mouse_focus_out(GtkWidget *widget,
			 GdkEvent *gdk_event_raw,
			 struct tme_gtk_screen *screen)
{
  struct tme_display *display;

  display = screen->screen.tme_screen_display;

  /* we won't see the releases of any keys held down now: */
  screen->tme_gtk_screen_mouse_mods = 0;

  /* release the mouse: */
  tme_mutex_lock(&display->tme_display_mutex);
  _tme_gtk_mouse_mode_off(screen, GDK_CURRENT_TIME);
  tme_mutex_unlock(&display->tme_display_mutex);

  /* continue propagating this event: */
  return (FALSE);
}

/* this is a GTK callback for a mouse event in the framebuffer: */
static int
_tme_gtk_mouse_mouse_event(GtkWidget *widget,
			   GdkEvent *gdk_event_raw,
			   struct tme_gtk_screen *screen)
{
  GdkWindow *window;
  gint x, y;
  gint window_x, window_y, width, height;
  int button;

  /* if mouse mode is off, a click in the screen turns it on.  the
     click itself isn't passed on: */
  if (!screen->tme_gtk_screen_mouse_on) {
    if (gdk_event_raw->type == GDK_BUTTON_PRESS) {
      _tme_gtk_mouse_mode_on(screen, gdk_event_raw);
    }
    return (TRUE);
  }

  /* we ignore double- and triple-click events, since normal button
     press and release events are always generated also: */
  if (gdk_event_raw->type == GDK_2BUTTON_PRESS
      || gdk_event_raw->type == GDK_3BUTTON_PRESS) {
    return (TRUE);
  }

  /* if this is motion: */
  button = 0;
  if (gdk_event_raw->type == GDK_MOTION_NOTIFY) {
    x = gdk_event_raw->motion.x_root;
    y = gdk_event_raw->motion.y_root;
  }

  /* otherwise, this must be a button press or a release: */
  else {
    assert (gdk_event_raw->type == GDK_BUTTON_PRESS
	    || gdk_event_raw->type == GDK_BUTTON_RELEASE);
    x = gdk_event_raw->button.x_root;
    y = gdk_event_raw->button.y_root;
    button = gdk_event_raw->button.button;
    if (gdk_event_raw->type == GDK_BUTTON_RELEASE) {
      button = -button;
    }
  }

  _tme_mouse_button_press(button, x, y, screen->screen.tme_screen_display);

  /* if the pointer has moved far from the middle of the screen, put
     it back there, so that it never reaches an edge: */
  window = gtk_widget_get_window(screen->tme_gtk_screen_gtkframe);
  gdk_window_get_origin(window, &window_x, &window_y);
  width = gdk_window_get_width(window);
  height = gdk_window_get_height(window);
  if (x < window_x + (width / 4)
      || x >= window_x + width - (width / 4)
      || y < window_y + (height / 4)
      || y >= window_y + height - (height / 4)) {
    _tme_gtk_mouse_recenter(screen);
  }

  /* stop propagating this event: */
  return (TRUE);
}

/* this attaches the GTK mouse to a new screen: */
void
_tme_gtk_mouse_attach(struct tme_gtk_screen *screen)
{
  GtkWidget *hbox0;

  /* create the horizontal packing box for the mouse controls: */
  hbox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  /* pack the horizontal packing box into the outer vertical packing box: */
  gtk_box_pack_start(GTK_BOX(screen->tme_gtk_screen_vbox0), 
		     hbox0,
		     FALSE, FALSE, 0);

  /* show the horizontal packing box: */
  gtk_widget_show(hbox0);

  /* create the mouse on label: */
  screen->tme_gtk_screen_mouse_label
    = gtk_label_new(NULL);

  /* pack the mouse on label into the horizontal packing box: */
  gtk_box_pack_start(GTK_BOX(hbox0), 
		     screen->tme_gtk_screen_mouse_label,
		     FALSE, FALSE, 0);

  /* show the mouse on label: */
  gtk_widget_show(screen->tme_gtk_screen_mouse_label);

  /* create the mouse statusbar: */
  screen->tme_gtk_screen_mouse_statusbar
    = gtk_statusbar_new();

  /* pack the mouse statusbar into the horizontal packing box: */
  gtk_box_pack_start(GTK_BOX(hbox0), 
		     screen->tme_gtk_screen_mouse_statusbar,
		     TRUE, TRUE, 10);

  /* show the mouse statusbar: */
  gtk_widget_show(screen->tme_gtk_screen_mouse_statusbar);
  screen->tme_gtk_screen_mouse_statusbar_cid
    = gtk_statusbar_get_context_id(GTK_STATUSBAR(screen->tme_gtk_screen_mouse_statusbar),
				   "mouse context");

  /* the framebuffer gets the mouse events: */
  gtk_widget_add_events(screen->tme_gtk_screen_gtkframe,
			GDK_POINTER_MOTION_MASK
			| GDK_BUTTON_PRESS_MASK
			| GDK_BUTTON_RELEASE_MASK);
  g_signal_connect(screen->tme_gtk_screen_gtkframe,
		   "motion_notify_event",
		   G_CALLBACK(_tme_gtk_mouse_mouse_event), 
		   screen);
  g_signal_connect(screen->tme_gtk_screen_gtkframe,
		   "button_press_event",
		   G_CALLBACK(_tme_gtk_mouse_mouse_event), 
		   screen);
  g_signal_connect(screen->tme_gtk_screen_gtkframe,
		   "button_release_event",
		   G_CALLBACK(_tme_gtk_mouse_mouse_event), 
		   screen);

  /* the mouse is released when the window loses the focus: */
  g_signal_connect(screen->tme_gtk_screen_window,
		   "focus_out_event",
		   G_CALLBACK(_tme_gtk_mouse_focus_out),
		   screen);

  /* mouse mode is off: */
  screen->tme_gtk_screen_mouse_on = FALSE;
  screen->tme_gtk_screen_mouse_mods = 0;
  _tme_gtk_mouse_status(screen);
}
