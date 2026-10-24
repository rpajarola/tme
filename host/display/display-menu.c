/* host/display/display-menu.c - menus and status for displays: */

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
#include "display.h"
#include <tme/media.h>
#include <tme/control.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#else  /* HAVE_STDARG_H */
#include <varargs.h>
#endif /* HAVE_STDARG_H */

/* the shortcut for Quit: */
#ifdef __APPLE__
#define TME_DISPLAY_MENU_QUIT_SHORTCUT	"Cmd-Q"
#else  /* !__APPLE__ */
#define TME_DISPLAY_MENU_QUIT_SHORTCUT	"Ctrl-Q"
#endif /* !__APPLE__ */

/* this makes a new, empty menu: */
struct tme_display_menu *
_tme_display_menu_new(void)
{
  return (tme_new0(struct tme_display_menu, 1));
}

/* this removes all of the items from a menu: */
void
_tme_display_menu_clear(struct tme_display_menu *menu)
{
  struct tme_display_menu_item *item;

  while ((item = menu->tme_display_menu_items) != NULL) {
    menu->tme_display_menu_items = item->tme_display_menu_item_next;
    if (item->tme_display_menu_item_submenu != NULL) {
      _tme_display_menu_free(item->tme_display_menu_item_submenu);
    }
    if (item->tme_display_menu_item_label != NULL) {
      tme_free(item->tme_display_menu_item_label);
    }
    if (item->tme_display_menu_item_shortcut != NULL) {
      tme_free(item->tme_display_menu_item_shortcut);
    }
    tme_free(item);
  }
}

/* this frees a menu: */
void
_tme_display_menu_free(struct tme_display_menu *menu)
{
  _tme_display_menu_clear(menu);
  tme_free(menu);
}

/* this adds an item to a menu after another item, or at the start if
   that is NULL.  a submenu item gets a new, empty submenu: */
struct tme_display_menu_item *
_tme_display_menu_insert(struct tme_display_menu *menu,
			 struct tme_display_menu_item *after,
			 unsigned int type,
			 const char *label,
			 tme_display_menu_func func,
			 void *private)
{
  struct tme_display_menu_item *item, **_prev;

  item = tme_new0(struct tme_display_menu_item, 1);
  item->tme_display_menu_item_type = type;
  if (label != NULL) {
    item->tme_display_menu_item_label = tme_strdup(label);
  }
  item->tme_display_menu_item_enabled = (type != TME_DISPLAY_MENU_ITEM_SEPARATOR);
  if (type == TME_DISPLAY_MENU_ITEM_SUBMENU) {
    item->tme_display_menu_item_submenu = _tme_display_menu_new();
  }
  item->tme_display_menu_item_func = func;
  item->tme_display_menu_item_private = private;

  _prev = (after == NULL
	   ? &menu->tme_display_menu_items
	   : &after->tme_display_menu_item_next);
  item->tme_display_menu_item_next = *_prev;
  *_prev = item;
  return (item);
}

/* this adds an item to the end of a menu: */
struct tme_display_menu_item *
_tme_display_menu_append(struct tme_display_menu *menu,
			 unsigned int type,
			 const char *label,
			 tme_display_menu_func func,
			 void *private)
{
  struct tme_display_menu_item *last;

  last = menu->tme_display_menu_items;
  if (last != NULL) {
    for (; last->tme_display_menu_item_next != NULL;
	 last = last->tme_display_menu_item_next);
  }
  return (_tme_display_menu_insert(menu, last, type, label, func, private));
}

/* this notes that a screen's menus have changed: */
void
_tme_display_menu_changed(struct tme_screen *screen)
{
  if (screen->tme_screen_menus != NULL) {
    screen->tme_screen_menus->tme_display_menus_generation++;
  }
}

/* this checks one radio item in a menu, and unchecks the others: */
static void
_tme_display_menu_radio_check(struct tme_display_menu *menu,
			      struct tme_display_menu_item *checked)
{
  struct tme_display_menu_item *item;

  for (item = menu->tme_display_menu_items;
       item != NULL;
       item = item->tme_display_menu_item_next) {
    if (item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_RADIO) {
      item->tme_display_menu_item_checked = (item == checked);
    }
  }
}

/* this returns the menu that holds an item, searching from a menu: */
static struct tme_display_menu *
_tme_display_menu_parent(struct tme_display_menu *menu,
			 const struct tme_display_menu_item *child)
{
  struct tme_display_menu_item *item;
  struct tme_display_menu *parent;

  for (item = menu->tme_display_menu_items;
       item != NULL;
       item = item->tme_display_menu_item_next) {
    if (item == child) {
      return (menu);
    }
    if (item->tme_display_menu_item_submenu != NULL) {
      parent = _tme_display_menu_parent(item->tme_display_menu_item_submenu, child);
      if (parent != NULL) {
	return (parent);
      }
    }
  }
  return (NULL);
}

/* this activates a menu item.  a check item is toggled, and a radio
   item is checked, before its function is called: */
void
_tme_display_menu_activate(struct tme_screen *screen,
			   struct tme_display_menu_item *item)
{
  struct tme_display_menu *parent;

  if (!item->tme_display_menu_item_enabled) {
    return;
  }
  switch (item->tme_display_menu_item_type) {
  case TME_DISPLAY_MENU_ITEM_CHECK:
    item->tme_display_menu_item_checked = !item->tme_display_menu_item_checked;
    break;
  case TME_DISPLAY_MENU_ITEM_RADIO:
    parent = _tme_display_menu_parent(screen->tme_screen_menus->tme_display_menus_bar, item);
    if (parent != NULL) {
      _tme_display_menu_radio_check(parent, item);
    }
    break;
  case TME_DISPLAY_MENU_ITEM_SUBMENU:
  case TME_DISPLAY_MENU_ITEM_SEPARATOR:
    return;
  default:
    break;
  }
  _tme_display_menu_changed(screen);
  if (item->tme_display_menu_item_func != NULL) {
    (*item->tme_display_menu_item_func)(screen, item);
  }
}

/* this sets a status line slot.  a NULL or empty text clears it.  if
   timeout is nonzero, the slot clears itself after that many
   milliseconds: */
void
_tme_display_status_set(struct tme_screen *screen,
			unsigned int slot,
			const char *text,
			unsigned int timeout)
{
  struct tme_display_menus *menus;
  char *old;

  menus = screen->tme_screen_menus;
  if (menus == NULL) {
    return;
  }
  old = menus->tme_display_menus_status[slot];
  if (text != NULL && text[0] == '\0') {
    text = NULL;
  }
  if (timeout == 0
      && (old == NULL
	  ? text == NULL
	  : (text != NULL && strcmp(old, text) == 0))) {
    menus->tme_display_menus_status_expires[slot] = 0;
    return;
  }
  if (old != NULL) {
    tme_free(old);
  }
  menus->tme_display_menus_status[slot] = (text == NULL ? NULL : tme_strdup(text));
  menus->tme_display_menus_status_expires[slot]
    = (text == NULL || timeout == 0
       ? 0
       : (_tme_display_event_time() + timeout) | 1);
  menus->tme_display_menus_status_generation++;
}

/* this returns the text in a status line slot, or NULL if it's
   empty: */
const char *
_tme_display_status_get(struct tme_screen *screen, unsigned int slot)
{
  return (screen->tme_screen_menus == NULL
	  ? NULL
	  : screen->tme_screen_menus->tme_display_menus_status[slot]);
}

/* this shows a message in the status line for a while: */
void
#ifdef HAVE_STDARG_H
_tme_display_message(struct tme_screen *screen, const char *format, ...)
#else  /* HAVE_STDARG_H */
_tme_display_message(screen, format, va_alist)
     struct tme_screen *screen;
     const char *format;
     va_dcl
#endif /* HAVE_STDARG_H */
{
  va_list args;
  char message[256];

#ifdef HAVE_STDARG_H
  va_start(args, format);
#else  /* HAVE_STDARG_H */
  va_start(args);
#endif /* HAVE_STDARG_H */
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (screen->tme_screen_display->tme_screen_message != NULL) {
    (*screen->tme_screen_display->tme_screen_message)(screen, message);
  }
  else {
    _tme_display_status_set(screen, TME_DISPLAY_STATUS_MESSAGE,
			    message, TME_DISPLAY_STATUS_MESSAGE_MSEC);
  }
}

/* this shows an error, in a dialog if the backend has them, else in
   the status line: */
static void
_tme_display_error(struct tme_screen *screen, const char *text)
{
  struct tme_display *display;

  display = screen->tme_screen_display;
  if (display->tme_screen_error != NULL) {
    (*display->tme_screen_error)(screen, text);
  }
  else {
    _tme_display_message(screen, "%s", text);
  }
}

/* a backend calls this when files have been chosen, or with NULL
   files when the chooser was canceled.  this frees the request: */
void
_tme_display_files_chosen(struct tme_screen *screen,
			  struct tme_display_files_request *request,
			  char **files)
{
  (*request->tme_display_files_request_done)(screen, request, files);
  tme_free(request->tme_display_files_request_title);
  if (request->tme_display_files_request_start != NULL) {
    tme_free(request->tme_display_files_request_start);
  }
  tme_free(request);
}

/* this is called for a Scale item: */
static void
_tme_display_menu_scale(struct tme_screen *screen,
			struct tme_display_menu_item *item)
{
  _tme_screen_scale_set(screen, item->tme_display_menu_item_value);
}

/* this shows any error output from a capture: */
static void
_tme_display_menu_capture_output(struct tme_screen *screen, int rc, char *output)
{
  if (output != NULL) {
    if (rc != TME_OK) {
      _tme_display_message(screen, "%s", output);
    }
    tme_free(output);
  }
}

/* this is called for the Save Screenshot item: */
static void
_tme_display_menu_screenshot(struct tme_screen *screen,
			     struct tme_display_menu_item *item)
{
  char *output;
  int rc;

  output = NULL;
  rc = _tme_display_screenshot(screen, NULL, &output);
  _tme_display_menu_capture_output(screen, rc, output);
}

/* this is called for the Record item: */
static void
_tme_display_menu_record(struct tme_screen *screen,
			 struct tme_display_menu_item *item)
{
  char *output;
  int rc;

  output = NULL;
  rc = TME_OK;
  if (item->tme_display_menu_item_checked) {
    if (!_tme_display_recording(screen)) {
      rc = _tme_display_record_start(screen, NULL, &output);
    }
  }
  else if (_tme_display_recording(screen)) {
    rc = _tme_display_record_stop(screen, &output);
  }
  _tme_display_menu_capture_output(screen, rc, output);
}

/* this is called for the Quit item: */
static void
_tme_display_menu_quit(struct tme_screen *screen,
		       struct tme_display_menu_item *item)
{
  (*screen->tme_screen_display->tme_screen_quit)(screen);
}

/* this opens tmesh's machine window on the machine's script, in
   another tmesh, so the machine keeps running.  tmesh says where the
   script is, and where tmesh is, in the environment: */
static void
_tme_display_menu_edit_config(struct tme_screen *screen,
			      struct tme_display_menu_item *item)
{
  const char *program, *config;
  pid_t pid;
  int fd;

  program = getenv("TMESH_PROGRAM");
  config = getenv("TMESH_CONFIG");
  if (program == NULL || config == NULL) {
    return;
  }

  /* the window runs in a grandchild, which init reaps: */
  pid = fork();
  if (pid == 0) {
    if (fork() == 0) {
      setsid();
      fd = open("/dev/null", O_RDWR);
      if (fd >= 0) {
	dup2(fd, STDIN_FILENO);
      }
      execl(program, program, "--edit", config, "--running", (char *) NULL);
      _exit(127);
    }
    _exit(0);
  }
  if (pid < 0) {
    _tme_display_message(screen, _("Could not open the machine window: %s"), strerror(errno));
    return;
  }
  while (waitpid(pid, NULL, 0) < 0 && errno == EINTR);
}

/* this checks the Scale item for the screen's scaling: */
static void
_tme_display_menu_scale_update(struct tme_screen *screen)
{
  struct tme_display_menu_item *item;
  int scale, checked;

  scale = screen->tme_screen_fb_scale;
  for (item = screen->tme_screen_menus->tme_display_menus_scale->tme_display_menu_items;
       item != NULL;
       item = item->tme_display_menu_item_next) {
    checked = (scale < 0
	       ? item->tme_display_menu_item_value < 0
	       : item->tme_display_menu_item_value == scale);
    if (checked != item->tme_display_menu_item_checked) {
      item->tme_display_menu_item_checked = checked;
      _tme_display_menu_changed(screen);
    }
  }
}

/* this returns the last part of a path: */
static const char *
_tme_display_basename(const char *path)
{
  const char *slash;

  slash = strrchr(path, '/');
  return (slash != NULL && slash[1] != '\0' ? slash + 1 : path);
}

/* this compares two strings for qsort(): */
static int
_tme_display_strcmp(const void *a, const void *b)
{
  return (strcmp(*(char * const *) a, *(char * const *) b));
}

/* this returns the regular files in a folder, other than hidden
   ones, sorted by name, in an array to be freed with
   tme_free_string_array(), or NULL if there are none: */
static char **
_tme_display_folder_files(const char *folder)
{
  DIR *dir;
  struct dirent *entry;
  struct stat st;
  char **files;
  char *path;
  unsigned int count, size;

  dir = opendir(folder);
  if (dir == NULL) {
    return (NULL);
  }
  count = 0;
  size = 16;
  files = tme_new(char *, size);
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    path = tme_new(char, strlen(folder) + 1 + strlen(entry->d_name) + 1);
    sprintf(path, "%s/%s", folder, entry->d_name);
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
      tme_free(path);
      continue;
    }
    if (count + 1 >= size) {
      size *= 2;
      files = tme_renew(char *, files, size);
    }
    files[count++] = path;
  }
  closedir(dir);
  files[count] = NULL;
  if (count == 0) {
    tme_free(files);
    return (NULL);
  }
  qsort(files, count, sizeof(files[0]), _tme_display_strcmp);
  return (files);
}

/* this returns a short description of what a media holder holds, to
   be freed with tme_free(): */
static char *
_tme_display_media_describe(struct tme_media *media)
{
  char **filenames;
  const char *base;
  char *description;
  int read_only, count;

  filenames = (*media->tme_media_loaded)(media, &read_only);
  if (filenames == NULL) {
    return (tme_strdup(_("empty")));
  }
  for (count = 0; filenames[count] != NULL; count++);
  base = _tme_display_basename(filenames[0]);
  description = tme_new(char, strlen(base) + 32);
  if (count > 1) {
    sprintf(description, _("%s and %d more"), base, count - 1);
  }
  else {
    strcpy(description, base);
  }
  tme_free_string_array(filenames, -1);
  return (description);
}

/* this shows an error from loading or unloading media: */
static void
_tme_display_media_error(struct tme_screen *screen,
			 struct tme_media *media,
			 const char *action,
			 int rc,
			 char *output)
{
  char message[512];

  snprintf(message, sizeof(message),
	   _("Could not %s %s: %s"),
	   action,
	   media->tme_media_name,
	   ((output != NULL && output[0] != '\0')
	    ? output
	    : strerror(rc)));
  _tme_display_error(screen, message);
}

/* this unloads a media holder, if it holds anything, returning
   nonzero on success: */
static int
_tme_display_media_unload(struct tme_screen *screen,
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
    _tme_display_media_error(screen, media, _("eject"), rc, output);
  }
  if (output != NULL) {
    tme_free(output);
  }
  return (rc == TME_OK);
}

/* this returns nonzero if a media holder is still registered.  the
   menus can be rebuilt while a chooser is open, but media holders
   are never unregistered, so this is only a sanity check: */
static int
_tme_display_media_valid(struct tme_media *check)
{
  struct tme_media *media;

  for (media = tme_media_list();
       media != NULL;
       media = media->tme_media_next) {
    if (media == check) {
      return (TRUE);
    }
  }
  return (FALSE);
}

/* this is called when files have been chosen to load: */
static void
_tme_display_media_load_done(struct tme_screen *screen,
			     struct tme_display_files_request *request,
			     char **files)
{
  struct tme_media *media;
  char **folder_files, **loaded;
  const char **filenames;
  char *output;
  int count, read_only, rc;

  media = request->tme_display_files_request_private;
  if (files == NULL
      || files[0] == NULL
      || !_tme_display_media_valid(media)) {
    return;
  }

  /* a folder chosen for a tape is all of the files in it, in name
     order.  tape files chosen one by one are loaded in name order
     too: */
  folder_files = NULL;
  if (request->tme_display_files_request_folder) {
    folder_files = _tme_display_folder_files(files[0]);
    if (folder_files == NULL) {
      _tme_display_message(screen, _("%s has no files"), files[0]);
      return;
    }
    files = folder_files;
  }
  for (count = 0; files[count] != NULL; count++);
  filenames = tme_new0(const char *, count + 1);
  memcpy(filenames, files, sizeof(files[0]) * count);
  qsort(filenames, count, sizeof(filenames[0]), _tme_display_strcmp);

  /* load the new media the same way as the old, after ejecting any
     old media: */
  read_only = FALSE;
  loaded = (*media->tme_media_loaded)(media, &read_only);
  if (loaded != NULL) {
    tme_free_string_array(loaded, -1);
  }
  if (_tme_display_media_unload(screen, media)) {
    output = NULL;
    rc = (*media->tme_media_load)(media, filenames, read_only, &output);
    if (rc != TME_OK) {
      _tme_display_media_error(screen, media, _("load"), rc, output);
    }
    if (output != NULL) {
      tme_free(output);
    }
  }
  tme_free(filenames);
  if (folder_files != NULL) {
    tme_free_string_array(folder_files, -1);
  }
}

/* this is called for the Load items: */
static void
_tme_display_media_load(struct tme_screen *screen,
			struct tme_display_menu_item *item)
{
  struct tme_media *media;
  struct tme_display_files_request *request;
  char **filenames;
  const char *format;
  char *slash;
  int read_only;

  media = item->tme_display_menu_item_private;
  request = tme_new0(struct tme_display_files_request, 1);
  request->tme_display_files_request_folder = item->tme_display_menu_item_value;
  request->tme_display_files_request_multiple
    = (media->tme_media_type == TME_MEDIA_TYPE_TAPE
       && !request->tme_display_files_request_folder);
  format = (request->tme_display_files_request_folder
	    ? _("Load %s from a folder of tape files")
	    : request->tme_display_files_request_multiple
	    ? _("Load %s (choose the tape files)")
	    : _("Load %s"));
  request->tme_display_files_request_title
    = tme_new(char, strlen(format) + strlen(media->tme_media_name) + 1);
  sprintf(request->tme_display_files_request_title, format, media->tme_media_name);

  /* start in the folder of what's loaded now, if anything: */
  filenames = (*media->tme_media_loaded)(media, &read_only);
  if (filenames != NULL) {
    request->tme_display_files_request_start = tme_strdup(filenames[0]);
    slash = strrchr(request->tme_display_files_request_start, '/');
    if (slash != NULL) {
      *(slash == request->tme_display_files_request_start ? slash + 1 : slash) = '\0';
    }
    else {
      tme_free(request->tme_display_files_request_start);
      request->tme_display_files_request_start = NULL;
    }
    tme_free_string_array(filenames, -1);
  }

  request->tme_display_files_request_done = _tme_display_media_load_done;
  request->tme_display_files_request_private = media;
  (*screen->tme_screen_display->tme_screen_files_choose)(screen, request);
}

/* this is called for the Eject items: */
static void
_tme_display_media_eject(struct tme_screen *screen,
			 struct tme_display_menu_item *item)
{
  _tme_display_media_unload(screen, item->tme_display_menu_item_private);
}

/* this rebuilds the Media menu and the media status, if the media
   have changed: */
static void
_tme_display_menu_media_update(struct tme_screen *screen)
{
  struct tme_display_menus *menus;
  struct tme_display_menu *menu, *submenu;
  struct tme_display_menu_item *item;
  struct tme_media *media;
  char **filenames;
  char *description, *label, *status;
  size_t status_size;
  int read_only, can_choose;
  unsigned long generation;

  /* if nothing has changed, return now: */
  menus = screen->tme_screen_menus;
  generation = tme_media_generation();
  if (generation == menus->tme_display_menus_media_generation) {
    return;
  }
  menus->tme_display_menus_media_generation = generation;

  can_choose = (screen->tme_screen_display->tme_screen_files_choose != NULL);
  menu = menus->tme_display_menus_media;
  _tme_display_menu_clear(menu);
  status_size = 1;
  status = tme_new0(char, status_size);
  for (media = tme_media_list();
       media != NULL;
       media = media->tme_media_next) {

    /* skip media holders that haven't been named yet, and ones whose
       media isn't removable, like hard disks: */
    if (media->tme_media_name == NULL
	|| !media->tme_media_removable) {
      continue;
    }

    /* the item for this media holder shows what it holds: */
    description = _tme_display_media_describe(media);
    label = tme_new(char, strlen(media->tme_media_name) + 2 + strlen(description) + 1);
    sprintf(label, "%s: %s", media->tme_media_name, description);
    item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_SUBMENU,
				    label, NULL, NULL);
    submenu = item->tme_display_menu_item_submenu;

    /* its submenu loads and ejects.  a tape can be loaded from a
       folder holding one file per tape file: */
    if (media->tme_media_type == TME_MEDIA_TYPE_TAPE) {
      item = _tme_display_menu_append(submenu, TME_DISPLAY_MENU_ITEM_ACTION,
				      _("Load Tape Files..."),
				      _tme_display_media_load, media);
      item->tme_display_menu_item_enabled = can_choose;
      item = _tme_display_menu_append(submenu, TME_DISPLAY_MENU_ITEM_ACTION,
				      _("Load Tape Folder..."),
				      _tme_display_media_load, media);
      item->tme_display_menu_item_enabled = can_choose;
      item->tme_display_menu_item_value = TRUE;
    }
    else {
      item = _tme_display_menu_append(submenu, TME_DISPLAY_MENU_ITEM_ACTION,
				      _("Load..."),
				      _tme_display_media_load, media);
      item->tme_display_menu_item_enabled = can_choose;
    }
    filenames = (*media->tme_media_loaded)(media, &read_only);
    item = _tme_display_menu_append(submenu, TME_DISPLAY_MENU_ITEM_ACTION,
				    _("Eject"),
				    _tme_display_media_eject, media);
    item->tme_display_menu_item_enabled = (filenames != NULL);
    if (filenames != NULL) {
      tme_free_string_array(filenames, -1);
    }

    /* add this media holder to the status: */
    status_size += strlen(label) + 4;
    status = tme_renew(char, status, status_size);
    if (status[0] != '\0') {
      strcat(status, "    ");
    }
    strcat(status, label);
    tme_free(label);
    tme_free(description);
  }
  if (status[0] == '\0') {
    item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_ACTION,
				    _("No removable media"), NULL, NULL);
    item->tme_display_menu_item_enabled = FALSE;
  }
  _tme_display_status_set(screen, TME_DISPLAY_STATUS_MEDIA, status, 0);
  tme_free(status);
  _tme_display_menu_changed(screen);
}

/* this is called for a Devices item: */
static void
_tme_display_menu_control(struct tme_screen *screen,
			  struct tme_display_menu_item *item)
{
  struct tme_controls *controls;
  unsigned int index;
  char *output;
  char message[512];
  int rc;

  controls = item->tme_display_menu_item_private;
  index = item->tme_display_menu_item_value;
  output = NULL;
  rc = (*controls->tme_controls_set)(controls, index,
				     item->tme_display_menu_item_checked,
				     &output);
  if (rc != TME_OK) {
    snprintf(message, sizeof(message),
	     _("Could not change %s %s: %s"),
	     controls->tme_controls_name,
	     controls->tme_controls_items[index].tme_control_item_label,
	     ((output != NULL && output[0] != '\0')
	      ? output
	      : strerror(rc)));
    _tme_display_error(screen, message);
  }
  else if (controls->tme_controls_items[index].tme_control_item_type
	   == TME_CONTROL_ITEM_ACTION) {
    _tme_display_message(screen, "%s: %s",
			 controls->tme_controls_name,
			 controls->tme_controls_items[index].tme_control_item_label);
  }
  if (output != NULL) {
    tme_free(output);
  }
}

/* this rebuilds the Devices menu if the controls have changed, and
   checks the switch items that are on: */
static void
_tme_display_menu_devices_update(struct tme_screen *screen)
{
  struct tme_display_menus *menus;
  struct tme_display_menu *menu;
  struct tme_display_menu_item *item, *control_item;
  struct tme_controls *controls;
  unsigned int index;
  unsigned long generation;
  int on;

  menus = screen->tme_screen_menus;
  menu = menus->tme_display_menus_devices;
  generation = tme_controls_generation();
  if (generation != menus->tme_display_menus_devices_generation) {
    menus->tme_display_menus_devices_generation = generation;
    _tme_display_menu_clear(menu);
    for (controls = tme_controls_list();
	 controls != NULL;
	 controls = controls->tme_controls_next) {

      /* skip controls that haven't been named yet: */
      if (controls->tme_controls_name == NULL) {
	continue;
      }
      item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_SUBMENU,
				      controls->tme_controls_name, NULL, NULL);
      for (index = 0; index < controls->tme_controls_count; index++) {
	control_item
	  = _tme_display_menu_append(item->tme_display_menu_item_submenu,
				     (controls->tme_controls_items[index].tme_control_item_type
				      == TME_CONTROL_ITEM_SWITCH
				      ? TME_DISPLAY_MENU_ITEM_CHECK
				      : TME_DISPLAY_MENU_ITEM_ACTION),
				     _(controls->tme_controls_items[index].tme_control_item_label),
				     _tme_display_menu_control, controls);
	control_item->tme_display_menu_item_value = index;
      }
    }
    if (menu->tme_display_menu_items == NULL) {
      item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_ACTION,
				      _("No devices"), NULL, NULL);
      item->tme_display_menu_item_enabled = FALSE;
    }
    _tme_display_menu_changed(screen);
  }

  /* switches can also be changed by commands, and by the machine: */
  for (item = menu->tme_display_menu_items;
       item != NULL;
       item = item->tme_display_menu_item_next) {
    if (item->tme_display_menu_item_submenu == NULL) {
      continue;
    }
    for (control_item = item->tme_display_menu_item_submenu->tme_display_menu_items;
	 control_item != NULL;
	 control_item = control_item->tme_display_menu_item_next) {
      if (control_item->tme_display_menu_item_type != TME_DISPLAY_MENU_ITEM_CHECK) {
	continue;
      }
      controls = control_item->tme_display_menu_item_private;
      on = ((*controls->tme_controls_get)(controls, control_item->tme_display_menu_item_value) != 0);
      if (on != control_item->tme_display_menu_item_checked) {
	control_item->tme_display_menu_item_checked = on;
	_tme_display_menu_changed(screen);
      }
    }
  }
}

/* this gives a screen the standard menus and a status line: */
void
_tme_display_menus_new(struct tme_screen *screen)
{
  struct tme_display_menus *menus;
  struct tme_display_menu *menu;
  struct tme_display_menu_item *item;
  static const struct {
    const char *label;
    int scale;
  } scales[] = {
    { "Default", -TME_FB_XLAT_SCALE_NONE },
    { "Half", TME_FB_XLAT_SCALE_HALF },
    { "Full", TME_FB_XLAT_SCALE_NONE },
    { "Double", TME_FB_XLAT_SCALE_DOUBLE },
  };
  unsigned int scale_i;

  menus = tme_new0(struct tme_display_menus, 1);
  screen->tme_screen_menus = menus;
  menus->tme_display_menus_bar = _tme_display_menu_new();

  /* the Screen menu: */
  item = _tme_display_menu_append(menus->tme_display_menus_bar,
				  TME_DISPLAY_MENU_ITEM_SUBMENU,
				  _("Screen"), NULL, NULL);
  menu = item->tme_display_menu_item_submenu;
  item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_SUBMENU,
				  _("Scale"), NULL, NULL);
  menus->tme_display_menus_scale = item->tme_display_menu_item_submenu;
  for (scale_i = 0; scale_i < TME_ARRAY_ELS(scales); scale_i++) {
    item = _tme_display_menu_append(menus->tme_display_menus_scale,
				    TME_DISPLAY_MENU_ITEM_RADIO,
				    _(scales[scale_i].label),
				    _tme_display_menu_scale, NULL);
    item->tme_display_menu_item_value = scales[scale_i].scale;
  }
  _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_SEPARATOR,
			   NULL, NULL, NULL);
  item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_ACTION,
				  _("Save Screenshot"),
				  _tme_display_menu_screenshot, NULL);
  item->tme_display_menu_item_enabled
    = (screen->tme_screen_display->tme_screen_capture != NULL);
  item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_CHECK,
				  _("Record"),
				  _tme_display_menu_record, NULL);
  item->tme_display_menu_item_enabled
    = (screen->tme_screen_display->tme_screen_capture != NULL);
  menus->tme_display_menus_record = item;
  _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_SEPARATOR,
			   NULL, NULL, NULL);
  item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_ACTION,
				  _("Edit Configuration..."),
				  _tme_display_menu_edit_config, NULL);
  item->tme_display_menu_item_enabled
    = (getenv("TMESH_PROGRAM") != NULL && getenv("TMESH_CONFIG") != NULL);
  item = _tme_display_menu_append(menu, TME_DISPLAY_MENU_ITEM_ACTION,
				  _("Quit"), _tme_display_menu_quit, NULL);
  item->tme_display_menu_item_shortcut = tme_strdup(TME_DISPLAY_MENU_QUIT_SHORTCUT);
  item->tme_display_menu_item_enabled
    = (screen->tme_screen_display->tme_screen_quit != NULL);

  /* the Media menu, filled in by _tme_display_menu_media_update(): */
  item = _tme_display_menu_append(menus->tme_display_menus_bar,
				  TME_DISPLAY_MENU_ITEM_SUBMENU,
				  _("Media"), NULL, NULL);
  menus->tme_display_menus_media = item->tme_display_menu_item_submenu;
  menus->tme_display_menus_media_generation = tme_media_generation() - 1;

  /* the Devices menu, filled in by _tme_display_menu_devices_update(): */
  item = _tme_display_menu_append(menus->tme_display_menus_bar,
				  TME_DISPLAY_MENU_ITEM_SUBMENU,
				  _("Devices"), NULL, NULL);
  menus->tme_display_menus_devices = item->tme_display_menu_item_submenu;
  menus->tme_display_menus_devices_generation = tme_controls_generation() - 1;

  _tme_display_menus_update(screen);
}

/* this brings a screen's menus and status line up to date.  the
   display calls this for every screen with menus, every time it
   updates: */
void
_tme_display_menus_update(struct tme_screen *screen)
{
  struct tme_display_menus *menus;
  tme_uint32_t now;
  unsigned int slot;

  menus = screen->tme_screen_menus;
  if (menus == NULL) {
    return;
  }

  /* update the menus.  a recording can start and stop from commands,
     or stop by itself: */
  _tme_display_menu_scale_update(screen);
  if (menus->tme_display_menus_record->tme_display_menu_item_checked
      != _tme_display_recording(screen)) {
    menus->tme_display_menus_record->tme_display_menu_item_checked
      = _tme_display_recording(screen);
    _tme_display_menu_changed(screen);
  }
  _tme_display_menu_media_update(screen);
  _tme_display_menu_devices_update(screen);

  /* clear any status line slots that have expired: */
  now = _tme_display_event_time();
  for (slot = 0; slot < TME_DISPLAY_STATUS_SLOTS; slot++) {
    if (menus->tme_display_menus_status_expires[slot] != 0
	&& (tme_int32_t) (now - menus->tme_display_menus_status_expires[slot]) >= 0) {
      _tme_display_status_set(screen, slot, NULL, 0);
    }
  }
}
