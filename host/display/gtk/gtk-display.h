/* $Id: gtk-display.h,v 1.10 2009/08/28 01:29:47 fredette Exp $ */

/* host/gtk/gtk-display.h - header file for GTK display support: */

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

#ifndef _HOST_GTK_DISPLAY_H
#define _HOST_GTK_DISPLAY_H

_TME_RCSID("$Id: gtk-display.h,v 1.10 2009/08/28 01:29:47 fredette Exp $");

/* includes: */
#include "display.h"
#ifndef G_ENABLE_DEBUG
#define G_ENABLE_DEBUG (0)
#endif /* !G_ENABLE_DEBUG */
#include <gtk/gtk.h>

/* macros: */

/* how deep the menus can be: */
#define TME_GTK_MENU_DEPTH		(8)

/* types: */

/* a display: */
struct tme_gdk_display {

  /* the generic display structure */
  struct tme_display display;

  GdkDisplay *tme_gdk_display;
  
  GdkCursor *tme_gdk_display_cursor;

  GdkSeat *tme_gdk_display_seat;

  GdkMonitor *tme_gdk_display_monitor;
};
  
/* a screen: */
struct tme_gtk_screen {

  /* the generic screen structure */
  struct tme_screen screen;

  /* the top-level window: */
  GtkWidget *tme_gtk_screen_window;
  
  /* the outer vertical packing box: */
  GtkWidget *tme_gtk_screen_vbox0;

  /* the GtkWidget, GdkWindow & cairo_surface for the framebuffer: */
  GtkWidget *tme_gtk_screen_gtkframe;
  cairo_surface_t *tme_gtk_screen_surface;
  cairo_format_t tme_gtk_screen_format;

  /* the mouse on label: */
  GtkWidget *tme_gtk_screen_mouse_label;

  /* the status bar, and the context ID: */
  GtkWidget *tme_gtk_screen_mouse_statusbar;
  guint tme_gtk_screen_mouse_statusbar_cid;

  /* nonzero iff mouse mode is on, and the pointer is captured: */
  int tme_gtk_screen_mouse_on;

  /* the modifier keys being held down, for the key combination
     that turns mouse mode off: */
  unsigned int tme_gtk_screen_mouse_mods;

  /* the menu bar, built from the shared menus, the accelerators for
     their shortcuts, and the menu generation it shows, if it has
     been built: */
  GtkWidget *tme_gtk_screen_menu_bar;
  GtkAccelGroup *tme_gtk_screen_accel_group;
  int tme_gtk_screen_menus_built;
  unsigned long tme_gtk_screen_menus_generation;

  /* the media status label, and the status generation it shows: */
  GtkWidget *tme_gtk_screen_media_label;
  unsigned long tme_gtk_screen_status_generation;

  /* the status bar context ID for messages: */
  guint tme_gtk_screen_capture_cid;
};

/* prototypes: */
void _tme_gtk_keyboard_attach _TME_P((struct tme_gtk_screen *));
void _tme_gtk_mouse_attach _TME_P((struct tme_gtk_screen *));
void _tme_gtk_mouse_mode_off _TME_P((struct tme_gtk_screen *, guint32));
void _tme_gtk_mouse_key_event _TME_P((struct tme_gtk_screen *, GdkEventKey *));
void _tme_gtk_menu_init _TME_P((struct tme_gdk_display *));
void _tme_gtk_menu_attach _TME_P((struct tme_gtk_screen *));
void _tme_gtk_menu_update _TME_P((struct tme_gtk_screen *));
void _tme_gtk_screen_menus_new _TME_P((struct tme_gtk_screen *));
gint _tme_display_enter_focus _TME_P((GtkWidget *, GdkEvent *, gpointer));

#endif /* _HOST_GTK_DISPLAY_H */

