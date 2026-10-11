/* host/display/gtk/gtk-media.c - GTK removable media support: */

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
#include <tme/media.h>
#include <string.h>

/* the key for the media holder on menu items and dialogs: */
#define TME_GTK_MEDIA_KEY	"tme-media"

/* the file chooser response for loading all of the files in the
   current folder as a tape: */
#define TME_GTK_MEDIA_RESPONSE_FOLDER	(1)

/* this returns the regular files in a folder, other than hidden
   ones, sorted by name: */
static GSList *
_tme_gtk_media_folder_files(const gchar *folder)
{
  GDir *dir;
  const gchar *name;
  gchar *path;
  GSList *files;

  files = NULL;
  dir = g_dir_open(folder, 0, NULL);
  if (dir == NULL) {
    return (NULL);
  }
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (name[0] == '.') {
      continue;
    }
    path = g_build_filename(folder, name, NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
      files = g_slist_prepend(files, path);
    }
    else {
      g_free(path);
    }
  }
  g_dir_close(dir);
  return (files);
}

/* this returns a short description of what a media holder holds, to
   be freed with g_free(): */
static gchar *
_tme_gtk_media_describe(struct tme_media *media)
{
  char **filenames;
  int read_only, count;
  gchar *base, *description;

  filenames = (*media->tme_media_loaded)(media, &read_only);
  if (filenames == NULL) {
    return (g_strdup(_("empty")));
  }
  for (count = 0; filenames[count] != NULL; count++);
  base = g_path_get_basename(filenames[0]);
  if (count > 1) {
    description = g_strdup_printf(_("%s and %d more"), base, count - 1);
  }
  else {
    description = g_strdup(base);
  }
  g_free(base);
  tme_free_string_array(filenames, -1);
  return (description);
}

/* this shows an error: */
static void
_tme_gtk_media_error(struct tme_gtk_screen *screen,
		     struct tme_media *media,
		     const char *action,
		     int rc,
		     char *output)
{
  GtkWidget *dialog;

  dialog
    = gtk_message_dialog_new(GTK_WINDOW(screen->tme_gtk_screen_window),
			     GTK_DIALOG_DESTROY_WITH_PARENT,
			     GTK_MESSAGE_ERROR,
			     GTK_BUTTONS_CLOSE,
			     _("Could not %s %s: %s"),
			     action,
			     media->tme_media_name,
			     ((output != NULL && output[0] != '\0')
			      ? output
			      : strerror(rc)));
  g_signal_connect_swapped(dialog, "response",
			   G_CALLBACK(gtk_widget_destroy), dialog);
  gtk_widget_show(dialog);
}

/* this unloads a media holder, if it holds anything, returning
   nonzero on success: */
static int
_tme_gtk_media_unload(struct tme_gtk_screen *screen,
		      struct tme_media *media)
{
  char **filenames;
  char *output;
  int read_only, rc;

  filenames = (*media->tme_media_loaded)(media, &read_only);
  if (filenames == NULL) {
    return (TRUE);
  }
  tme_free_string_array(filenames, -1);
  output = NULL;
  rc = (*media->tme_media_unload)(media, &output);
  if (rc != TME_OK) {
    _tme_gtk_media_error(screen, media, _("eject"), rc, output);
  }
  if (output != NULL) {
    tme_free(output);
  }
  return (rc == TME_OK);
}

/* this is called when the file chooser for loading media is done: */
static void
_tme_gtk_media_load_response(GtkDialog *dialog,
			     gint response,
			     struct tme_gtk_screen *screen)
{
  struct tme_media *media;
  GSList *files, *file;
  const char **filenames;
  char **loaded;
  char *output;
  int count, read_only, rc;

  media = g_object_get_data(G_OBJECT(dialog), TME_GTK_MEDIA_KEY);
  if (response == GTK_RESPONSE_ACCEPT
      || response == TME_GTK_MEDIA_RESPONSE_FOLDER) {

    /* get the files, either the ones chosen, or all of the files in
       the folder shown.  tape segments are loaded in name order: */
    if (response == GTK_RESPONSE_ACCEPT) {
      files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
    }
    else {
      gchar *folder;
      folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog));
      files = (folder == NULL ? NULL : _tme_gtk_media_folder_files(folder));
      g_free(folder);
    }
    files = g_slist_sort(files, (GCompareFunc) strcmp);
    count = g_slist_length(files);
    filenames = tme_new0(const char *, count + 1);
    count = 0;
    for (file = files; file != NULL; file = file->next) {
      filenames[count++] = file->data;
    }

    /* load the new media the same way as the old, after ejecting
       any old media: */
    loaded = (*media->tme_media_loaded)(media, &read_only);
    if (loaded != NULL) {
      tme_free_string_array(loaded, -1);
    }
    if (count > 0 && _tme_gtk_media_unload(screen, media)) {
      output = NULL;
      rc = (*media->tme_media_load)(media, filenames, read_only, &output);
      if (rc != TME_OK) {
	_tme_gtk_media_error(screen, media, _("load"), rc, output);
      }
      if (output != NULL) {
	tme_free(output);
      }
    }
    tme_free(filenames);
    g_slist_free_full(files, g_free);
  }
  gtk_widget_destroy(GTK_WIDGET(dialog));
}

/* this is called for the Load menu item: */
static void
_tme_gtk_media_load(GtkWidget *menu_item,
		    struct tme_gtk_screen *screen)
{
  struct tme_media *media;
  GtkWidget *dialog;
  char **filenames;
  gchar *title, *folder;
  int read_only;

  media = g_object_get_data(G_OBJECT(menu_item), TME_GTK_MEDIA_KEY);
  title = g_strdup_printf((media->tme_media_type == TME_MEDIA_TYPE_TAPE
			   ? _("Load %s (choose tape files, or a whole folder)")
			   : _("Load %s")),
			  media->tme_media_name);
  dialog
    = gtk_file_chooser_dialog_new(title,
				  GTK_WINDOW(screen->tme_gtk_screen_window),
				  GTK_FILE_CHOOSER_ACTION_OPEN,
				  _("_Cancel"), GTK_RESPONSE_CANCEL,
				  _("_Load"), GTK_RESPONSE_ACCEPT,
				  NULL);
  g_free(title);
  g_object_set_data(G_OBJECT(dialog), TME_GTK_MEDIA_KEY, media);
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog),
				       media->tme_media_type == TME_MEDIA_TYPE_TAPE);

  /* a tape is often a folder of files, one per tape file: */
  if (media->tme_media_type == TME_MEDIA_TYPE_TAPE) {
    gtk_dialog_add_button(GTK_DIALOG(dialog),
			  _("Load Whole _Folder"),
			  TME_GTK_MEDIA_RESPONSE_FOLDER);
  }

  /* start in the folder of what's loaded now, if anything: */
  filenames = (*media->tme_media_loaded)(media, &read_only);
  if (filenames != NULL) {
    folder = g_path_get_dirname(filenames[0]);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), folder);
    g_free(folder);
    tme_free_string_array(filenames, -1);
  }

  /* the dialog isn't modal, since running it would stop the
     emulator: */
  g_signal_connect(dialog, "response",
		   G_CALLBACK(_tme_gtk_media_load_response), screen);
  gtk_widget_show(dialog);
}

/* this is called for the Eject menu item: */
static void
_tme_gtk_media_eject(GtkWidget *menu_item,
		     struct tme_gtk_screen *screen)
{
  _tme_gtk_media_unload(screen,
			g_object_get_data(G_OBJECT(menu_item), TME_GTK_MEDIA_KEY));
}

/* this adds an item to a menu: */
static GtkWidget *
_tme_gtk_media_menu_item(GtkWidget *menu, const char *label,
			 GCallback callback,
			 struct tme_gtk_screen *screen,
			 struct tme_media *media)
{
  GtkWidget *menu_item;

  menu_item = gtk_menu_item_new_with_label(label);
  if (callback != NULL) {
    g_object_set_data(G_OBJECT(menu_item), TME_GTK_MEDIA_KEY, media);
    g_signal_connect(menu_item, "activate", callback, screen);
  }
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
  gtk_widget_show(menu_item);
  return (menu_item);
}

/* this updates the Media menu and the media status, if anything has
   changed: */
void
_tme_gtk_media_update(struct tme_gtk_screen *screen)
{
  struct tme_media *media;
  GtkWidget *menu, *menu_item, *submenu;
  GString *status;
  gchar *description, *label;
  char **filenames;
  int read_only;
  unsigned long generation;

  /* if nothing has changed, return now: */
  generation = tme_media_generation();
  if (generation == screen->tme_gtk_screen_media_generation) {
    return;
  }
  screen->tme_gtk_screen_media_generation = generation;

  /* make a new Media menu: */
  menu = gtk_menu_new();
  status = g_string_new(NULL);
  for (media = tme_media_list();
       media != NULL;
       media = media->tme_media_next) {

    /* skip media holders that haven't been named yet, and ones
       whose media isn't removable, like hard disks: */
    if (media->tme_media_name == NULL
	|| !media->tme_media_removable) {
      continue;
    }

    /* the item for this media holder shows what it holds: */
    description = _tme_gtk_media_describe(media);
    label = g_strdup_printf("%s: %s", media->tme_media_name, description);
    menu_item = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);
    g_free(label);

    /* its submenu loads and ejects: */
    submenu = gtk_menu_new();
    _tme_gtk_media_menu_item(submenu,
			     (media->tme_media_type == TME_MEDIA_TYPE_TAPE
			      ? _("Load Tape Files...")
			      : _("Load...")),
			     G_CALLBACK(_tme_gtk_media_load), screen, media);
    filenames = (*media->tme_media_loaded)(media, &read_only);
    gtk_widget_set_sensitive(_tme_gtk_media_menu_item(submenu, _("Eject"),
						      G_CALLBACK(_tme_gtk_media_eject),
						      screen, media),
			     filenames != NULL);
    if (filenames != NULL) {
      tme_free_string_array(filenames, -1);
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);

    /* add this media holder to the status: */
    g_string_append_printf(status, "%s%s: %s",
			   (status->len > 0 ? "    " : ""),
			   media->tme_media_name,
			   description);
    g_free(description);
  }
  if (status->len == 0) {
    menu_item = _tme_gtk_media_menu_item(menu, _("No removable media"), NULL, screen, NULL);
    gtk_widget_set_sensitive(menu_item, FALSE);
  }

  /* replace the Media menu, and the status: */
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(screen->tme_gtk_screen_media_menu_item), menu);
  gtk_label_set_text(GTK_LABEL(screen->tme_gtk_screen_media_label), status->str);
  gtk_widget_set_visible(screen->tme_gtk_screen_media_label, status->len > 0);
  g_string_free(status, TRUE);
}

/* this attaches the Media menu and the media status to a new
   screen: */
void
_tme_gtk_media_attach(struct tme_gtk_screen *screen, GtkWidget *menu_bar)
{

  /* the Media menu, filled in by _tme_gtk_media_update(): */
  screen->tme_gtk_screen_media_menu_item = gtk_menu_item_new_with_label(_("Media"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), screen->tme_gtk_screen_media_menu_item);

  /* the media status, below everything else: */
  screen->tme_gtk_screen_media_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(screen->tme_gtk_screen_media_label), 0.0);
  gtk_label_set_ellipsize(GTK_LABEL(screen->tme_gtk_screen_media_label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_margin_start(screen->tme_gtk_screen_media_label, 4);
  gtk_widget_set_margin_end(screen->tme_gtk_screen_media_label, 4);
  gtk_box_pack_end(GTK_BOX(screen->tme_gtk_screen_vbox0),
		   screen->tme_gtk_screen_media_label,
		   FALSE, FALSE, 2);

  /* force the first update: */
  screen->tme_gtk_screen_media_generation = tme_media_generation() - 1;
  _tme_gtk_media_update(screen);
}
