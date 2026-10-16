/* host/display/sdl/sdl-dialog.c - SDL2 dialogs: */

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

/* the SDL display draws its dialogs in the window, over everything
   else, instead of using SDL's message boxes, which are modal and
   would stop the emulated machine while they are up.  a dialog takes
   all keys and mouse events while it is open, and dialogs opened
   while one is open wait their turn: */

#include <tme/common.h>

/* includes: */
#include "sdl-display.h"
#include <string.h>

/* the most characters in a line of a dialog's message: */
#define TME_SDL_DIALOG_COLUMNS		(60)

/* the layout of a dialog, in pixels: */
struct tme_sdl_dialog_layout {

  /* the dialog: */
  SDL_Rect box;

  /* the message, in lines of at most this many characters: */
  int columns;
  int lines;

  /* the buttons: */
  SDL_Rect buttons[TME_SDL_DIALOG_BUTTONS];
};

/* this finds the next line of a message that fits in some number of
   characters, breaking it at a space if it can.  it returns the
   number of bytes in the line, and advances the text past the line
   and the space or newline that ends it: */
static size_t
_tme_sdl_dialog_line(const char **_text, int columns)
{
  const char *text, *next, *space;
  size_t length;
  int count;

  text = *_text;
  next = text;
  space = NULL;
  for (count = 0; *next != '\0' && *next != '\n'; count++) {
    if (count == columns) {
      if (space != NULL) {
	length = space - text;
	*_text = space + 1;
	return (length);
      }
      break;
    }
    if (*next == ' ') {
      space = next;
    }
    _tme_sdl_text_next(&next);
  }
  length = next - text;
  *_text = (*next == '\n' ? next + 1 : next);
  return (length);
}

/* this lays out a dialog: */
static void
_tme_sdl_dialog_layout(struct tme_sdl_screen *screen,
		       const struct tme_sdl_dialog *dialog,
		       struct tme_sdl_dialog_layout *layout)
{
  const char *text, *line;
  char *copy;
  size_t length;
  int fw, fh, pad, border, output_width, output_height;
  int width, buttons_width, button_i, x, longest;

  fw = screen->tme_sdl_screen_font_width;
  fh = screen->tme_sdl_screen_font_height;
  pad = screen->tme_sdl_screen_pad;
  border = _tme_sdl_menu_border(screen);
  _tme_sdl_output_size(screen, &output_width, &output_height);

  /* the message is as wide as its longest line, which is at most as
     wide as fits in the window: */
  layout->columns = TME_MAX(TME_MIN(TME_SDL_DIALOG_COLUMNS, output_width / fw - 6), 10);
  layout->lines = 0;
  longest = _tme_sdl_text_length(dialog->tme_sdl_dialog_title);
  for (text = dialog->tme_sdl_dialog_text; *text != '\0'; layout->lines++) {
    line = text;
    length = _tme_sdl_dialog_line(&text, layout->columns);
    copy = tme_dup(char, line, length + 1);
    copy[length] = '\0';
    longest = TME_MAX(longest, _tme_sdl_text_length(copy));
    tme_free(copy);
  }
  longest = TME_MIN(longest, layout->columns);

  /* the buttons are at the bottom right: */
  buttons_width = 0;
  for (button_i = 0; button_i < dialog->tme_sdl_dialog_button_count; button_i++) {
    layout->buttons[button_i].w
      = (_tme_sdl_text_length(dialog->tme_sdl_dialog_buttons[button_i]) + 4) * fw;
    layout->buttons[button_i].h = fh + 2 * pad;
    buttons_width += layout->buttons[button_i].w + (button_i > 0 ? fw : 0);
  }

  width = TME_MAX(longest * fw, buttons_width);
  layout->box.w = width + 4 * fw + 2 * border;
  layout->box.h = (2 * border
		   + 3 * pad		/* above the title */
		   + fh			/* the title */
		   + 4 * pad		/* the line under the title */
		   + layout->lines * fh
		   + 5 * pad		/* above the buttons */
		   + fh + 2 * pad	/* the buttons */
		   + 3 * pad);		/* below the buttons */
  layout->box.x = TME_MAX((output_width - layout->box.w) / 2, 0);
  layout->box.y = TME_MAX((output_height - layout->box.h) / 2, 0);

  x = layout->box.x + layout->box.w - border - 2 * fw - buttons_width;
  for (button_i = 0; button_i < dialog->tme_sdl_dialog_button_count; button_i++) {
    layout->buttons[button_i].x = x;
    layout->buttons[button_i].y = (layout->box.y + layout->box.h
				   - border - 3 * pad - layout->buttons[button_i].h);
    x += layout->buttons[button_i].w + fw;
  }
}

/* this draws the dialog that is showing, if there is one: */
void
_tme_sdl_dialog_draw(struct tme_sdl_screen *screen)
{
  const struct tme_sdl_dialog *dialog;
  struct tme_sdl_dialog_layout layout;
  const char *text, *line;
  char *copy;
  size_t length;
  const SDL_Rect *button;
  Uint32 color;
  int fw, fh, pad, border, x, y, button_i;

  dialog = screen->tme_sdl_screen_dialogs;
  if (dialog == NULL) {
    return;
  }
  _tme_sdl_dialog_layout(screen, dialog, &layout);
  fw = screen->tme_sdl_screen_font_width;
  fh = screen->tme_sdl_screen_font_height;
  pad = screen->tme_sdl_screen_pad;
  border = _tme_sdl_menu_border(screen);

  /* the box: */
  _tme_sdl_fill(screen, layout.box.x, layout.box.y, layout.box.w, layout.box.h,
		TME_SDL_COLOR_BORDER);
  _tme_sdl_fill(screen, layout.box.x + border, layout.box.y + border,
		layout.box.w - 2 * border, layout.box.h - 2 * border,
		TME_SDL_COLOR_MENU);

  /* the title, with a line under it: */
  x = layout.box.x + border + 2 * fw;
  y = layout.box.y + border + 3 * pad;
  _tme_sdl_text_draw(screen, x, y, dialog->tme_sdl_dialog_title,
		     TME_SDL_COLOR_TEXT, layout.columns);
  y += fh + 2 * pad;
  _tme_sdl_fill(screen, layout.box.x + border + fw, y,
		layout.box.w - 2 * border - 2 * fw, border,
		TME_SDL_COLOR_SEPARATOR);
  y += 2 * pad;

  /* the message: */
  for (text = dialog->tme_sdl_dialog_text; *text != '\0'; y += fh) {
    line = text;
    length = _tme_sdl_dialog_line(&text, layout.columns);
    copy = tme_dup(char, line, length + 1);
    copy[length] = '\0';
    _tme_sdl_text_draw(screen, x, y, copy, TME_SDL_COLOR_TEXT, layout.columns);
    tme_free(copy);
  }

  /* the buttons.  the highlighted one is in the highlight colors: */
  for (button_i = 0; button_i < dialog->tme_sdl_dialog_button_count; button_i++) {
    button = &layout.buttons[button_i];
    _tme_sdl_fill(screen, button->x, button->y, button->w, button->h,
		  TME_SDL_COLOR_BORDER);
    if (button_i == dialog->tme_sdl_dialog_button_highlight) {
      color = TME_SDL_COLOR_HIGHLIGHT;
    }
    else {
      color = TME_SDL_COLOR_BAR;
    }
    _tme_sdl_fill(screen, button->x + border, button->y + border,
		  button->w - 2 * border, button->h - 2 * border,
		  color);
    _tme_sdl_text_draw(screen, button->x + 2 * fw, button->y + pad,
		       dialog->tme_sdl_dialog_buttons[button_i],
		       (button_i == dialog->tme_sdl_dialog_button_highlight
			? TME_SDL_COLOR_HIGHLIGHT_TEXT
			: TME_SDL_COLOR_TEXT),
		       layout.columns);
  }
}

/* this closes the dialog that is showing, and calls its function
   with the button chosen: */
static void
_tme_sdl_dialog_close(struct tme_sdl_screen *screen, int button)
{
  struct tme_sdl_dialog *dialog;

  dialog = screen->tme_sdl_screen_dialogs;
  screen->tme_sdl_screen_dialogs = dialog->tme_sdl_dialog_next;
  _tme_sdl_screen_present(screen);
  if (dialog->tme_sdl_dialog_done != NULL) {
    (*dialog->tme_sdl_dialog_done)(screen, button);
  }
  tme_free(dialog->tme_sdl_dialog_title);
  tme_free(dialog->tme_sdl_dialog_text);
  tme_free(dialog);
}

/* this handles a mouse event while a dialog is showing.  x and y are
   in pixels.  a button is chosen by pressing and releasing the mouse
   button on it: */
void
_tme_sdl_dialog_mouse_event(struct tme_sdl_screen *screen,
			    const SDL_Event *event,
			    int x, int y)
{
  struct tme_sdl_dialog *dialog;
  struct tme_sdl_dialog_layout layout;
  const SDL_Rect *rect;
  int button_i, hit;

  dialog = screen->tme_sdl_screen_dialogs;
  _tme_sdl_dialog_layout(screen, dialog, &layout);
  hit = -1;
  for (button_i = 0; button_i < dialog->tme_sdl_dialog_button_count; button_i++) {
    rect = &layout.buttons[button_i];
    if (x >= rect->x && x < rect->x + rect->w
	&& y >= rect->y && y < rect->y + rect->h) {
      hit = button_i;
    }
  }
  switch (event->type) {
  case SDL_MOUSEBUTTONDOWN:
    dialog->tme_sdl_dialog_button_pressed = hit;
    if (hit >= 0) {
      dialog->tme_sdl_dialog_button_highlight = hit;
      _tme_sdl_screen_present(screen);
    }
    break;
  case SDL_MOUSEBUTTONUP:
    if (hit >= 0 && hit == dialog->tme_sdl_dialog_button_pressed) {
      _tme_sdl_dialog_close(screen, hit);
      return;
    }
    dialog->tme_sdl_dialog_button_pressed = -1;
    break;
  default:
    break;
  }
}

/* this handles a key event while a dialog is showing.  Return and
   Space choose the highlighted button, Escape chooses the cancel
   button, and the arrow keys and Tab move the highlight: */
void
_tme_sdl_dialog_key_event(struct tme_sdl_screen *screen,
			  const SDL_Event *event)
{
  struct tme_sdl_dialog *dialog;
  int count;

  if (event->type != SDL_KEYDOWN) {
    return;
  }
  dialog = screen->tme_sdl_screen_dialogs;
  count = dialog->tme_sdl_dialog_button_count;
  switch (event->key.keysym.sym) {
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
  case SDLK_SPACE:
    _tme_sdl_dialog_close(screen, dialog->tme_sdl_dialog_button_highlight);
    return;
  case SDLK_ESCAPE:
    _tme_sdl_dialog_close(screen, dialog->tme_sdl_dialog_button_cancel);
    return;
  case SDLK_LEFT:
    dialog->tme_sdl_dialog_button_highlight
      = (dialog->tme_sdl_dialog_button_highlight + count - 1) % count;
    break;
  case SDLK_RIGHT:
  case SDLK_TAB:
    dialog->tme_sdl_dialog_button_highlight
      = (dialog->tme_sdl_dialog_button_highlight + 1) % count;
    break;
  default:
    return;
  }
  _tme_sdl_screen_present(screen);
}

/* this opens a dialog, or queues it if one is already showing.  done
   is called with the index of the button chosen, and can be NULL: */
void
_tme_sdl_dialog_open(struct tme_sdl_screen *screen,
		     const char *title,
		     const char *text,
		     const char * const *buttons,
		     int button_count,
		     int button_default,
		     int button_cancel,
		     void (*done) _TME_P((struct tme_sdl_screen *, int)))
{
  struct tme_sdl_dialog *dialog, **_prev;
  struct tme_sdl_display *display;
  int button_i;

  dialog = tme_new0(struct tme_sdl_dialog, 1);
  dialog->tme_sdl_dialog_title = tme_strdup(title);
  dialog->tme_sdl_dialog_text = tme_strdup(text);
  button_count = TME_MIN(button_count, TME_SDL_DIALOG_BUTTONS);
  for (button_i = 0; button_i < button_count; button_i++) {
    dialog->tme_sdl_dialog_buttons[button_i] = buttons[button_i];
  }
  dialog->tme_sdl_dialog_button_count = button_count;
  dialog->tme_sdl_dialog_button_default = button_default;
  dialog->tme_sdl_dialog_button_cancel = button_cancel;
  dialog->tme_sdl_dialog_button_highlight = button_default;
  dialog->tme_sdl_dialog_button_pressed = -1;
  dialog->tme_sdl_dialog_done = done;
  for (_prev = &screen->tme_sdl_screen_dialogs;
       *_prev != NULL;
       _prev = &(*_prev)->tme_sdl_dialog_next);
  *_prev = dialog;

  /* the mouse and the keys held down go back to the host, and any
     open menus close: */
  display = _tme_sdl_screen_display(screen);
  _tme_sdl_mouse_mode_off(display);
  _tme_sdl_keys_release(display);
  screen->tme_sdl_screen_menu_depth = 0;
  _tme_sdl_screen_present(screen);
}

/* this returns nonzero if a dialog with a function is showing or
   waiting: */
int
_tme_sdl_dialog_find(struct tme_sdl_screen *screen,
		     void (*done) _TME_P((struct tme_sdl_screen *, int)))
{
  const struct tme_sdl_dialog *dialog;

  for (dialog = screen->tme_sdl_screen_dialogs;
       dialog != NULL;
       dialog = dialog->tme_sdl_dialog_next) {
    if (dialog->tme_sdl_dialog_done == done) {
      return (TRUE);
    }
  }
  return (FALSE);
}
