/* host/display/gtk/gtk-menu.c - GTK menus and status: */

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

#include <tme/common.h>

/* includes: */
#include "gtk-display.h"
#include <string.h>

/* the key for a menu item's path on its widget: */
#define TME_GTK_MENU_PATH_KEY	"tme-menu-path"

/* the key for a files request on a file chooser: */
#define TME_GTK_MENU_FILES_KEY	"tme-files-request"

/* the GTK widgets only refer to the shared menu items by their path
   from the menu bar, a list of item indices ending with -1, since
   activating an item can rebuild the shared menus: */

/* this returns the shared menu item at a path, or NULL: */
static struct tme_display_menu_item *
_tme_gtk_menu_path_item(struct tme_gtk_screen *screen, const int *path)
{
  struct tme_display_menu *menu;
  struct tme_display_menu_item *item;
  int index;

  if (screen->screen.tme_screen_menus == NULL) {
    return (NULL);
  }
  menu = screen->screen.tme_screen_menus->tme_display_menus_bar;
  item = NULL;
  for (; *path >= 0; path++) {
    if (menu == NULL) {
      return (NULL);
    }
    item = menu->tme_display_menu_items;
    for (index = *path; item != NULL && index > 0; index--) {
      item = item->tme_display_menu_item_next;
    }
    if (item == NULL) {
      return (NULL);
    }
    menu = item->tme_display_menu_item_submenu;
  }
  return (item);
}

/* this is called when a GTK menu item is activated: */
static void
_tme_gtk_menu_activate(GtkWidget *widget, struct tme_gtk_screen *screen)
{
  struct tme_display_menu_item *item;

  item = _tme_gtk_menu_path_item(screen,
				 g_object_get_data(G_OBJECT(widget), TME_GTK_MENU_PATH_KEY));
  if (item == NULL) {
    return;
  }

  /* GTK also activates the radio item that a click unchecks: */
  if (item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_RADIO
      && !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
    return;
  }
  _tme_display_menu_activate(&screen->screen, item);
}

/* this adds the accelerator for a shortcut like "Cmd-Q" or "Ctrl-Q"
   to a GTK menu item: */
static void
_tme_gtk_menu_shortcut(struct tme_gtk_screen *screen,
		       GtkWidget *widget,
		       const char *shortcut)
{
  const char *key;
  gchar *accelerator;
  guint accel_key;
  GdkModifierType accel_mods;

  key = strchr(shortcut, '-');
  if (key == NULL || key[1] == '\0') {
    return;
  }
  accelerator = g_strdup_printf("<Primary>%s", key + 1);
  gtk_accelerator_parse(accelerator, &accel_key, &accel_mods);
  g_free(accelerator);
  if (accel_key != 0) {
    gtk_widget_add_accelerator(widget, "activate",
			       screen->tme_gtk_screen_accel_group,
			       gdk_keyval_to_lower(accel_key), accel_mods,
			       GTK_ACCEL_VISIBLE);
  }
}

/* this fills a GTK menu shell with the items of a shared menu: */
static void
_tme_gtk_menu_build(struct tme_gtk_screen *screen,
		    GtkMenuShell *shell,
		    const struct tme_display_menu *menu,
		    int *path,
		    int depth)
{
  const struct tme_display_menu_item *item;
  GtkWidget *widget, *submenu;
  GSList *group;
  int index, *item_path;

  group = NULL;
  for (item = menu->tme_display_menu_items, index = 0;
       item != NULL;
       item = item->tme_display_menu_item_next, index++) {
    path[depth] = index;
    path[depth + 1] = -1;

    switch (item->tme_display_menu_item_type) {
    case TME_DISPLAY_MENU_ITEM_SEPARATOR:
      widget = gtk_separator_menu_item_new();
      break;
    case TME_DISPLAY_MENU_ITEM_CHECK:
      widget = gtk_check_menu_item_new_with_label(item->tme_display_menu_item_label);
      break;
    case TME_DISPLAY_MENU_ITEM_RADIO:
      widget = gtk_radio_menu_item_new_with_label(group, item->tme_display_menu_item_label);
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(widget));
      break;
    default:
      widget = gtk_menu_item_new_with_label(item->tme_display_menu_item_label);
      break;
    }

    /* a group of radio items ends at any other item: */
    if (item->tme_display_menu_item_type != TME_DISPLAY_MENU_ITEM_RADIO) {
      group = NULL;
    }

    /* set the check before connecting the signal, since setting it
       activates the item: */
    if (item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_CHECK
	|| item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_RADIO) {
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(widget),
				     item->tme_display_menu_item_checked);
    }
    gtk_widget_set_sensitive(widget, item->tme_display_menu_item_enabled);

    if (item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_SUBMENU) {
      submenu = gtk_menu_new();
      if (depth + 2 < TME_GTK_MENU_DEPTH) {
	_tme_gtk_menu_build(screen, GTK_MENU_SHELL(submenu),
			    item->tme_display_menu_item_submenu,
			    path, depth + 1);
      }
      gtk_menu_item_set_submenu(GTK_MENU_ITEM(widget), submenu);
    }
    else if (item->tme_display_menu_item_type != TME_DISPLAY_MENU_ITEM_SEPARATOR) {
      item_path = g_new(int, depth + 2);
      memcpy(item_path, path, sizeof(int) * (depth + 2));
      g_object_set_data_full(G_OBJECT(widget), TME_GTK_MENU_PATH_KEY,
			     item_path, g_free);
      g_signal_connect(widget, "activate",
		       G_CALLBACK(_tme_gtk_menu_activate), screen);
      if (item->tme_display_menu_item_shortcut != NULL) {
	_tme_gtk_menu_shortcut(screen, widget,
			       item->tme_display_menu_item_shortcut);
      }
    }
    gtk_menu_shell_append(shell, widget);
  }
}

/* this brings a screen's GTK menus and media status up to date with
   the shared ones: */
void
_tme_gtk_menu_update(struct tme_gtk_screen *screen)
{
  struct tme_display_menus *menus;
  GList *children, *child;
  int path[TME_GTK_MENU_DEPTH];
  const char *media;

  /* give a new screen its menus: */
  menus = screen->screen.tme_screen_menus;
  if (menus == NULL) {
    _tme_display_menus_new(&screen->screen);
    _tme_gtk_screen_menus_new(screen);
    menus = screen->screen.tme_screen_menus;
  }

  /* rebuild the menu bar if the menus have changed: */
  if (!screen->tme_gtk_screen_menus_built
      || menus->tme_display_menus_generation
      != screen->tme_gtk_screen_menus_generation) {
    screen->tme_gtk_screen_menus_built = TRUE;
    screen->tme_gtk_screen_menus_generation = menus->tme_display_menus_generation;
    children = gtk_container_get_children(GTK_CONTAINER(screen->tme_gtk_screen_menu_bar));
    for (child = children; child != NULL; child = child->next) {
      gtk_widget_destroy(GTK_WIDGET(child->data));
    }
    g_list_free(children);
    _tme_gtk_menu_build(screen, GTK_MENU_SHELL(screen->tme_gtk_screen_menu_bar),
			menus->tme_display_menus_bar, path, 0);
    gtk_widget_show_all(screen->tme_gtk_screen_menu_bar);
  }

  /* show the media status: */
  if (menus->tme_display_menus_status_generation
      != screen->tme_gtk_screen_status_generation) {
    screen->tme_gtk_screen_status_generation = menus->tme_display_menus_status_generation;
    media = _tme_display_status_get(&screen->screen, TME_DISPLAY_STATUS_MEDIA);
    gtk_label_set_text(GTK_LABEL(screen->tme_gtk_screen_media_label),
		       (media != NULL ? media : ""));
    gtk_widget_set_visible(screen->tme_gtk_screen_media_label, media != NULL);
  }
}

/* this is called when a file chooser is done: */
static void
_tme_gtk_menu_files_response(GtkDialog *dialog,
			     gint response,
			     struct tme_gtk_screen *screen)
{
  struct tme_display_files_request *request;
  GSList *files, *file;
  char **chosen;
  int count;

  request = g_object_get_data(G_OBJECT(dialog), TME_GTK_MENU_FILES_KEY);
  chosen = NULL;
  if (response == GTK_RESPONSE_ACCEPT) {
    files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
    chosen = tme_new0(char *, g_slist_length(files) + 1);
    count = 0;
    for (file = files; file != NULL; file = file->next) {
      chosen[count++] = tme_strdup(file->data);
    }
    g_slist_free_full(files, g_free);
  }
  gtk_widget_destroy(GTK_WIDGET(dialog));
  _tme_display_files_chosen(&screen->screen, request, chosen);
  if (chosen != NULL) {
    tme_free_string_array(chosen, -1);
  }
}

/* this starts choosing files.  the chooser isn't modal, since running
   it would stop the emulator: */
static void
_tme_gtk_menu_files_choose(struct tme_screen *_screen,
			   struct tme_display_files_request *request)
{
  struct tme_gtk_screen *screen;
  GtkWidget *dialog;
  int folder;

  screen = (struct tme_gtk_screen *) _screen;
  folder = request->tme_display_files_request_folder;
  dialog
    = gtk_file_chooser_dialog_new(request->tme_display_files_request_title,
				  GTK_WINDOW(screen->tme_gtk_screen_window),
				  (folder
				   ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
				   : GTK_FILE_CHOOSER_ACTION_OPEN),
				  _("_Cancel"), GTK_RESPONSE_CANCEL,
				  (folder ? _("_Choose") : _("_Load")), GTK_RESPONSE_ACCEPT,
				  NULL);
  g_object_set_data(G_OBJECT(dialog), TME_GTK_MENU_FILES_KEY, request);
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog),
				       request->tme_display_files_request_multiple);
  if (request->tme_display_files_request_start != NULL) {
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
					request->tme_display_files_request_start);
  }
  g_signal_connect(dialog, "response",
		   G_CALLBACK(_tme_gtk_menu_files_response), screen);
  gtk_widget_show(dialog);
}

/* this shows an error: */
static void
_tme_gtk_menu_error(struct tme_screen *_screen, const char *text)
{
  struct tme_gtk_screen *screen;
  GtkWidget *dialog;

  screen = (struct tme_gtk_screen *) _screen;
  dialog
    = gtk_message_dialog_new(GTK_WINDOW(screen->tme_gtk_screen_window),
			     GTK_DIALOG_DESTROY_WITH_PARENT,
			     GTK_MESSAGE_ERROR,
			     GTK_BUTTONS_CLOSE,
			     "%s", text);
  g_signal_connect_swapped(dialog, "response",
			   G_CALLBACK(gtk_widget_destroy), dialog);
  gtk_widget_show(dialog);
}

/* this attaches the menu bar and the media status to a new screen.
   the menus are filled in by _tme_gtk_menu_update(): */
void
_tme_gtk_menu_attach(struct tme_gtk_screen *screen)
{

  /* the menu bar, at the top: */
  screen->tme_gtk_screen_menu_bar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(screen->tme_gtk_screen_vbox0),
		     screen->tme_gtk_screen_menu_bar,
		     FALSE, FALSE, 0);
  screen->tme_gtk_screen_accel_group = gtk_accel_group_new();
  gtk_window_add_accel_group(GTK_WINDOW(screen->tme_gtk_screen_window),
			     screen->tme_gtk_screen_accel_group);
  screen->tme_gtk_screen_menus_built = FALSE;

  /* the media status, below everything else: */
  screen->tme_gtk_screen_media_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(screen->tme_gtk_screen_media_label), 0.0);
  gtk_label_set_ellipsize(GTK_LABEL(screen->tme_gtk_screen_media_label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_margin_start(screen->tme_gtk_screen_media_label, 4);
  gtk_widget_set_margin_end(screen->tme_gtk_screen_media_label, 4);
  gtk_widget_set_no_show_all(screen->tme_gtk_screen_media_label, TRUE);
  gtk_box_pack_end(GTK_BOX(screen->tme_gtk_screen_vbox0),
		   screen->tme_gtk_screen_media_label,
		   FALSE, FALSE, 2);
}

/* this sets the display functions that the shared menus use: */
void
_tme_gtk_menu_init(struct tme_gdk_display *display)
{
  display->display.tme_screen_files_choose = _tme_gtk_menu_files_choose;
  display->display.tme_screen_error = _tme_gtk_menu_error;
}
