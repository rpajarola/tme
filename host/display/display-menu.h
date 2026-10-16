/* host/display/display-menu.h - menus and status for displays: */

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

/* a display backend that draws its own menus and status line gets
   them from here.  the menus are a tree of items that the backend
   only shows and activates; what the items do is here, so every
   backend's menus do the same things.

   the menus and the status line are only used by the display
   thread, and never with the display mutex held.  anything that
   changes them bumps a generation number, so a backend knows when
   to draw them again.  activating an item can rebuild menus, so a
   backend must not keep pointers to items across an activation: */

#ifndef _HOST_DISPLAY_MENU_H
#define _HOST_DISPLAY_MENU_H

#include <tme/common.h>

/* macros: */

/* the menu item types: */
#define TME_DISPLAY_MENU_ITEM_ACTION	(0)
#define TME_DISPLAY_MENU_ITEM_CHECK	(1)
#define TME_DISPLAY_MENU_ITEM_RADIO	(2)
#define TME_DISPLAY_MENU_ITEM_SUBMENU	(3)
#define TME_DISPLAY_MENU_ITEM_SEPARATOR	(4)

/* the status line slots, shown left to right: */
#define TME_DISPLAY_STATUS_MOUSE	(0)
#define TME_DISPLAY_STATUS_MESSAGE	(1)
#define TME_DISPLAY_STATUS_MEDIA	(2)
#define TME_DISPLAY_STATUS_SLOTS	(3)

/* how long a message stays in the status line, in milliseconds: */
#define TME_DISPLAY_STATUS_MESSAGE_MSEC	(5000)

/* types: */

struct tme_screen;
struct tme_display_menu;
struct tme_display_menu_item;

/* the function called when a menu item is activated: */
typedef void (*tme_display_menu_func) _TME_P((struct tme_screen *,
					      struct tme_display_menu_item *));

/* a menu item: */
struct tme_display_menu_item {

  /* the next item in the menu: */
  struct tme_display_menu_item *tme_display_menu_item_next;

  /* the item type: */
  unsigned int tme_display_menu_item_type;

  /* the label, and any shortcut to show next to it, like "Cmd-Q".
     a separator has neither: */
  char *tme_display_menu_item_label;
  char *tme_display_menu_item_shortcut;

  /* nonzero if the item can be activated: */
  int tme_display_menu_item_enabled;

  /* for a check or radio item, nonzero if the item is checked: */
  int tme_display_menu_item_checked;

  /* for a submenu item, the submenu: */
  struct tme_display_menu *tme_display_menu_item_submenu;

  /* the function called when the item is activated, and any data for
     it: */
  tme_display_menu_func tme_display_menu_item_func;
  void *tme_display_menu_item_private;
  int tme_display_menu_item_value;
};

/* a menu: */
struct tme_display_menu {

  /* the items, in order: */
  struct tme_display_menu_item *tme_display_menu_items;
};

/* a request to choose files, for a backend that can: */
struct tme_display_files_request {

  /* the title for the chooser: */
  char *tme_display_files_request_title;

  /* nonzero if more than one file can be chosen: */
  int tme_display_files_request_multiple;

  /* nonzero if a folder is chosen, instead of files: */
  int tme_display_files_request_folder;

  /* any folder to start in: */
  char *tme_display_files_request_start;

  /* the function called with the files chosen, a NULL-terminated
     array of paths, or NULL if the chooser was canceled, and any data
     for it: */
  void (*tme_display_files_request_done) _TME_P((struct tme_screen *,
						 struct tme_display_files_request *,
						 char **));
  void *tme_display_files_request_private;
};

/* the per-screen menu and status state: */
struct tme_display_menus {

  /* the menu bar, a menu of submenu items: */
  struct tme_display_menu *tme_display_menus_bar;

  /* this changes whenever any menu changes: */
  unsigned long tme_display_menus_generation;

  /* the Scale submenu, and the Media menu and the media generation
     that it shows: */
  struct tme_display_menu *tme_display_menus_scale;
  struct tme_display_menu *tme_display_menus_media;
  unsigned long tme_display_menus_media_generation;

  /* the status line slots, and when each expires, or zero if it
     doesn't: */
  char *tme_display_menus_status[TME_DISPLAY_STATUS_SLOTS];
  tme_uint32_t tme_display_menus_status_expires[TME_DISPLAY_STATUS_SLOTS];

  /* this changes whenever the status line changes: */
  unsigned long tme_display_menus_status_generation;
};

/* prototypes: */

/* menus: */
struct tme_display_menu *_tme_display_menu_new _TME_P((void));
void _tme_display_menu_clear _TME_P((struct tme_display_menu *));
void _tme_display_menu_free _TME_P((struct tme_display_menu *));
struct tme_display_menu_item *_tme_display_menu_append _TME_P((struct tme_display_menu *,
							       unsigned int,
							       const char *,
							       tme_display_menu_func,
							       void *));
void _tme_display_menu_activate _TME_P((struct tme_screen *,
					struct tme_display_menu_item *));
void _tme_display_menu_changed _TME_P((struct tme_screen *));

/* the standard menus: */
void _tme_display_menus_new _TME_P((struct tme_screen *));
void _tme_display_menus_update _TME_P((struct tme_screen *));

/* the status line: */
void _tme_display_status_set _TME_P((struct tme_screen *, unsigned int,
				     const char *, unsigned int));
const char *_tme_display_status_get _TME_P((struct tme_screen *, unsigned int));
void _tme_display_message _TME_P((struct tme_screen *, const char *, ...));

/* choosing files: */
void _tme_display_files_chosen _TME_P((struct tme_screen *,
				       struct tme_display_files_request *,
				       char **));

#endif /* _HOST_DISPLAY_MENU_H */
