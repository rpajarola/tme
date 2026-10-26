/* host/display/sdl/sdl-display.c - SDL2 display support: */

/*
 * Copyright (c) 2022 Ruben Agin
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

/* includes: */
#include "sdl-display.h"
#include "sdl-icon.h"
#include <stdlib.h>
#include <string.h>

/* SDL needs its windows and events on the main thread on macOS, and
   the display runs them on its own thread unless the threads are
   cooperative: */
#if defined(__APPLE__) && !defined(TME_THREADS_SJLJ)
#define TME_SDL_THREADS_UNSUPPORTED
#endif

/* the modifier keys that can make up the key combination that turns
   mouse mode off: */
#define TME_SDL_MOUSE_MOD_CTRL		TME_BIT(0)
#define TME_SDL_MOUSE_MOD_SHIFT		TME_BIT(1)
#define TME_SDL_MOUSE_MOD_ALT		TME_BIT(2)
#define TME_SDL_MOUSE_MOD_CMD		TME_BIT(3)

/* the key combination that turns mouse mode off, and the modifier
   for the key that quits.  Ctrl+Shift with
   either Alt (Option) or Cmd (Meta, Super) works everywhere, but
   suggest the usual one for the platform: */
#ifdef __APPLE__
#define TME_SDL_MOUSE_OFF_KEYS		"Ctrl+Shift+Cmd"
#define TME_SDL_KMOD_PRIMARY		KMOD_GUI
#else
#define TME_SDL_MOUSE_OFF_KEYS		"Ctrl+Shift+Alt"
#define TME_SDL_KMOD_PRIMARY		KMOD_CTRL
#endif

/* the height of the menu bar and the status line together, in
   logical pixels, for picking the default scaling: */
#define TME_SDL_BARS_HEIGHT		(2 * (16 + 6))

/* the bell: */
#define TME_SDL_BELL_RATE		(22050)
#define TME_SDL_BELL_HZ			(880)
#define TME_SDL_BELL_MSEC		(100)
#define TME_SDL_BELL_VOLUME		(4000)

/* the keysyms for the SDL keys that don't type characters.  Cmd is
   Meta on macOS: */
static const struct {
  SDL_Keycode sdl_keycode;
  const char *keysym_name;
} _tme_sdl_keysyms[] = {
  { SDLK_BACKSPACE, "BackSpace" },
  { SDLK_TAB, "Tab" },
  { SDLK_CLEAR, "Clear" },
  { SDLK_RETURN, "Return" },
  { SDLK_RETURN2, "Return" },
  { SDLK_PAUSE, "Pause" },
  { SDLK_ESCAPE, "Escape" },
  { SDLK_DELETE, "Delete" },
  { SDLK_KP_0, "KP_0" },
  { SDLK_KP_1, "KP_1" },
  { SDLK_KP_2, "KP_2" },
  { SDLK_KP_3, "KP_3" },
  { SDLK_KP_4, "KP_4" },
  { SDLK_KP_5, "KP_5" },
  { SDLK_KP_6, "KP_6" },
  { SDLK_KP_7, "KP_7" },
  { SDLK_KP_8, "KP_8" },
  { SDLK_KP_9, "KP_9" },
  { SDLK_KP_PERIOD, "KP_Decimal" },
  { SDLK_KP_COMMA, "KP_Separator" },
  { SDLK_KP_DIVIDE, "KP_Divide" },
  { SDLK_KP_MULTIPLY, "KP_Multiply" },
  { SDLK_KP_MINUS, "KP_Subtract" },
  { SDLK_KP_PLUS, "KP_Add" },
  { SDLK_KP_ENTER, "KP_Enter" },
  { SDLK_KP_EQUALS, "KP_Equal" },
  { SDLK_UP, "Up" },
  { SDLK_DOWN, "Down" },
  { SDLK_RIGHT, "Right" },
  { SDLK_LEFT, "Left" },
  { SDLK_INSERT, "Insert" },
  { SDLK_HOME, "Home" },
  { SDLK_END, "End" },
  { SDLK_PAGEUP, "Page_Up" },
  { SDLK_PAGEDOWN, "Page_Down" },
  { SDLK_F1, "F1" },
  { SDLK_F2, "F2" },
  { SDLK_F3, "F3" },
  { SDLK_F4, "F4" },
  { SDLK_F5, "F5" },
  { SDLK_F6, "F6" },
  { SDLK_F7, "F7" },
  { SDLK_F8, "F8" },
  { SDLK_F9, "F9" },
  { SDLK_F10, "F10" },
  { SDLK_F11, "F11" },
  { SDLK_F12, "F12" },
  { SDLK_F13, "F13" },
  { SDLK_F14, "F14" },
  { SDLK_F15, "F15" },
  { SDLK_F16, "F16" },
  { SDLK_F17, "F17" },
  { SDLK_F18, "F18" },
  { SDLK_F19, "F19" },
  { SDLK_F20, "F20" },
  { SDLK_F21, "F21" },
  { SDLK_F22, "F22" },
  { SDLK_F23, "F23" },
  { SDLK_F24, "F24" },
  { SDLK_NUMLOCKCLEAR, "Num_Lock" },
  { SDLK_CAPSLOCK, "Caps_Lock" },
  { SDLK_SCROLLLOCK, "Scroll_Lock" },
  { SDLK_RSHIFT, "Shift_R" },
  { SDLK_LSHIFT, "Shift_L" },
  { SDLK_RCTRL, "Control_R" },
  { SDLK_LCTRL, "Control_L" },
  { SDLK_RALT, "Alt_R" },
  { SDLK_LALT, "Alt_L" },
#ifdef __APPLE__
  { SDLK_LGUI, "Meta_L" },
  { SDLK_RGUI, "Meta_R" },
#else
  { SDLK_LGUI, "Super_L" },
  { SDLK_RGUI, "Super_R" },
#endif
  { SDLK_MODE, "Mode_switch" },
  { SDLK_HELP, "Help" },
  { SDLK_PRINTSCREEN, "Print" },
  { SDLK_SYSREQ, "Sys_Req" },
  { SDLK_MENU, "Menu" },
  { SDLK_APPLICATION, "Menu" },
  { SDLK_UNDO, "Undo" },
  { SDLK_AGAIN, "Redo" },
  { SDLK_FIND, "Find" },
  { SDLK_STOP, "Cancel" },
  { SDLK_CANCEL, "Cancel" },
  { SDLK_EXECUTE, "Execute" },
  { SDLK_SELECT, "Select" },
  { SDLK_CUT, "Cut" },
  { SDLK_COPY, "Copy" },
  { SDLK_PASTE, "Paste" },
  { SDLK_MUTE, "AudioMute" },
  { SDLK_VOLUMEUP, "AudioRaiseVolume" },
  { SDLK_VOLUMEDOWN, "AudioLowerVolume" },
  { SDLK_POWER, "PowerOff" },
};

/* the shifted characters on a US keyboard, for when SDL doesn't say
   what character a key typed: */
static const char _tme_sdl_us_unshifted[] = "1234567890-=[]\\;',./`";
static const char _tme_sdl_us_shifted[]   = "!@#$%^&*()_+{}|:\"<>?~";

/* this logs an SDL error: */
static void
_tme_sdl_log_error(struct tme_display *display, const char *what)
{
  tme_log(&display->tme_display_element->tme_element_log_handle, 0, EIO,
	  (&display->tme_display_element->tme_element_log_handle,
	   _("%s: %s"), what, SDL_GetError()));
}

/* this returns the screen for an SDL window ID, or NULL: */
static struct tme_sdl_screen *
_tme_sdl_screen_from_id(Uint32 window_id)
{
  SDL_Window *window;

  window = SDL_GetWindowFromID(window_id);
  return (window == NULL
	  ? NULL
	  : (struct tme_sdl_screen *) SDL_GetWindowData(window, "tme"));
}

/* this shows what to do with the mouse in the status lines: */
static void
_tme_sdl_display_mouse_hint(struct tme_sdl_display *display)
{
  struct tme_screen *screen;

  for (screen = display->display.tme_display_screens;
       screen != NULL;
       screen = screen->tme_screen_next) {
    _tme_display_status_set(screen, TME_DISPLAY_STATUS_MOUSE,
			    (display->tme_sdl_display_mouse_on
			     ? _("Press " TME_SDL_MOUSE_OFF_KEYS " to release the mouse")
			     : _("Click in the screen to use the mouse, F10 for the menus")),
			    0);
  }
}

/* this turns mouse mode on: */
static void
_tme_sdl_mouse_mode_on(struct tme_sdl_display *display)
{
  if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0) {
    _tme_sdl_log_error(&display->display, _("cannot capture the mouse"));
    return;
  }
  display->tme_sdl_display_mouse_on = TRUE;
  _tme_sdl_display_mouse_hint(display);
}

/* this turns mouse mode off: */
void
_tme_sdl_mouse_mode_off(struct tme_sdl_display *display)
{
  if (!display->tme_sdl_display_mouse_on) {
    return;
  }
  SDL_SetRelativeMouseMode(SDL_FALSE);
  display->tme_sdl_display_mouse_on = FALSE;
  _tme_sdl_display_mouse_hint(display);
}

/* this turns mouse mode off when the key combination for that is
   pressed.  the keys are still passed on to the emulated keyboard: */
static void
_tme_sdl_mouse_key_event(struct tme_sdl_display *display,
			 const SDL_KeyboardEvent *event)
{
  unsigned int mod;
  unsigned int mods;

  switch (event->keysym.sym) {
  case SDLK_LCTRL:
  case SDLK_RCTRL:
    mod = TME_SDL_MOUSE_MOD_CTRL;
    break;
  case SDLK_LSHIFT:
  case SDLK_RSHIFT:
    mod = TME_SDL_MOUSE_MOD_SHIFT;
    break;
  case SDLK_LALT:
  case SDLK_RALT:
    mod = TME_SDL_MOUSE_MOD_ALT;
    break;
  case SDLK_LGUI:
  case SDLK_RGUI:
    mod = TME_SDL_MOUSE_MOD_CMD;
    break;
  default:
    return;
  }

  mods = display->tme_sdl_display_mouse_mods;
  if (event->type == SDL_KEYDOWN) {
    mods |= mod;
  }
  else {
    mods &= ~mod;
  }
  display->tme_sdl_display_mouse_mods = mods;

  if (event->type == SDL_KEYDOWN
      && (mods & TME_SDL_MOUSE_MOD_CTRL)
      && (mods & TME_SDL_MOUSE_MOD_SHIFT)
      && (mods & (TME_SDL_MOUSE_MOD_ALT
		  | TME_SDL_MOUSE_MOD_CMD))) {
    _tme_sdl_mouse_mode_off(display);
  }
}

/* this handles a mouse event: */
static void
_tme_sdl_mouse_event(struct tme_sdl_display *display,
		     const SDL_Event *event)
{
  struct tme_sdl_screen *screen;
  int x, y, button;

  /* if mouse mode is off, the mouse works the menus, and a click in
     the screen turns mouse mode on.  the click itself isn't passed
     on: */
  if (!display->tme_sdl_display_mouse_on) {
    screen = _tme_sdl_screen_from_id(event->type == SDL_MOUSEMOTION
				     ? event->motion.windowID
				     : event->button.windowID);
    if (screen != NULL
	&& _tme_sdl_menu_mouse_event(screen, event)) {
      return;
    }
    if (event->type == SDL_MOUSEBUTTONDOWN) {
      _tme_sdl_mouse_mode_on(display);
    }
    return;
  }

  /* the generic mouse support makes the motion from the change in
     the position, so move the position by the relative motion: */
  x = display->display.tme_screen_mouse_warp_x;
  y = display->display.tme_screen_mouse_warp_y;
  button = 0;
  if (event->type == SDL_MOUSEMOTION) {
    x += event->motion.xrel;
    y += event->motion.yrel;
  }
  else {
    button = event->button.button;
    if (event->type == SDL_MOUSEBUTTONUP) {
      button = -button;
    }
  }
  _tme_mouse_button_press(button, x, y, &display->display);
}

/* this decodes UTF-8 text that is exactly one character, returning
   the character, or -1: */
static long
_tme_sdl_utf8_char(const char *text)
{
  const unsigned char *s;
  long c;
  int more;

  s = (const unsigned char *) text;
  if (s[0] < 0x80) {
    c = s[0];
    more = 0;
  }
  else if ((s[0] & 0xe0) == 0xc0) {
    c = s[0] & 0x1f;
    more = 1;
  }
  else if ((s[0] & 0xf0) == 0xe0) {
    c = s[0] & 0x0f;
    more = 2;
  }
  else if ((s[0] & 0xf8) == 0xf0) {
    c = s[0] & 0x07;
    more = 3;
  }
  else {
    return (-1);
  }
  for (s++; more > 0; more--, s++) {
    if ((s[0] & 0xc0) != 0x80) {
      return (-1);
    }
    c = (c << 6) | (s[0] & 0x3f);
  }
  return ((c == 0 || s[0] != '\0') ? -1 : c);
}

/* this returns the keysym for a key press or release.  for a press of
   a key that types a character, it takes the text input event that
   SDL queues after the key event, since that knows the keyboard
   layout and the modifiers: */
static tme_keyboard_keyval_t
_tme_sdl_key_keysym(const SDL_KeyboardEvent *event)
{
  SDL_Keycode sym;
  SDL_Event next;
  const char *c;
  long ch;
  unsigned int key_i;

  /* a key that doesn't type a character: */
  sym = event->keysym.sym;
  for (key_i = 0; key_i < TME_ARRAY_ELS(_tme_sdl_keysyms); key_i++) {
    if (_tme_sdl_keysyms[key_i].sdl_keycode == sym) {
      return (_tme_keysym_from_name(_tme_sdl_keysyms[key_i].keysym_name));
    }
  }

  /* the Latin-1 keysyms are the same as the characters: */
  if (sym < 0x20 || sym > 0xff || (sym >= 0x7f && sym < 0xa0)) {
    return (TME_KEYBOARD_KEYVAL_UNDEF);
  }

  /* if this is a press, and the next event is the text that it
     typed, use that if it's one character in Latin-1: */
  if (event->type == SDL_KEYDOWN
      && SDL_PeepEvents(&next, 1, SDL_PEEKEVENT,
			SDL_FIRSTEVENT, SDL_LASTEVENT) == 1
      && next.type == SDL_TEXTINPUT) {
    ch = _tme_sdl_utf8_char(next.text.text);
    if (ch >= 0x20 && ch <= 0xff && (ch < 0x7f || ch >= 0xa0)) {
      SDL_PeepEvents(&next, 1, SDL_GETEVENT, SDL_TEXTINPUT, SDL_TEXTINPUT);
      return (ch);
    }
  }

  /* otherwise, apply Shift and Caps Lock as on a US keyboard: */
  if (sym >= 'a' && sym <= 'z') {
    if (!(event->keysym.mod & KMOD_SHIFT) != !(event->keysym.mod & KMOD_CAPS)) {
      sym -= 'a' - 'A';
    }
  }
  else if ((event->keysym.mod & KMOD_SHIFT)
	   && (c = strchr(_tme_sdl_us_unshifted, sym)) != NULL) {
    sym = _tme_sdl_us_shifted[c - _tme_sdl_us_unshifted];
  }
  return (sym);
}

/* this releases all keys held down, for when the window loses the
   focus and won't see their releases: */
void
_tme_sdl_keys_release(struct tme_sdl_display *display)
{
  unsigned int scancode;
  tme_keyboard_keyval_t keysym;

  for (scancode = 0; scancode < SDL_NUM_SCANCODES; scancode++) {
    keysym = display->tme_sdl_display_keys_down[scancode];
    if (keysym != TME_KEYBOARD_KEYVAL_UNDEF) {
      display->tme_sdl_display_keys_down[scancode] = TME_KEYBOARD_KEYVAL_UNDEF;
      _tme_keyboard_key_press(FALSE, keysym, &display->display);
    }
  }
  display->tme_sdl_display_mouse_mods = 0;
}

/* this is called when the quit dialog is answered: */
static void
_tme_sdl_display_quit_done(struct tme_sdl_screen *screen, int button)
{
  if (button == 1) {
    exit(0);
  }
}

/* this asks whether to quit the emulator, in a screen's window, or the
   first screen's if screen is NULL: */
static void
_tme_sdl_display_quit(struct tme_sdl_display *display,
		      struct tme_sdl_screen *screen)
{
  static const char * const buttons[] = { "Cancel", "Quit" };

  if (screen == NULL) {
    screen = (struct tme_sdl_screen *) display->display.tme_display_screens;
    if (screen == NULL) {
      exit(0);
    }
  }
  if (!_tme_sdl_dialog_find(screen, _tme_sdl_display_quit_done)) {
    _tme_sdl_dialog_open(screen,
			 _("Quit the emulator?"),
			 _("The emulated machine will stop immediately."),
			 buttons, TME_ARRAY_ELS(buttons), 1, 0,
			 _tme_sdl_display_quit_done);
  }
}

/* this asks whether to quit, for the Quit menu item: */
static void
_tme_sdl_screen_quit(struct tme_screen *screen)
{
  _tme_sdl_display_quit((struct tme_sdl_display *) screen->tme_screen_display,
			(struct tme_sdl_screen *) screen);
}

/* this shows an error: */
static void
_tme_sdl_screen_error(struct tme_screen *screen, const char *text)
{
  static const char * const buttons[] = { "OK" };

  _tme_sdl_dialog_open((struct tme_sdl_screen *) screen,
		       _("Error"), text,
		       buttons, TME_ARRAY_ELS(buttons), 0, 0,
		       NULL);
}

/* this handles a key press or release: */
static void
_tme_sdl_key_event(struct tme_sdl_display *display,
		   const SDL_Event *event)
{
  const SDL_KeyboardEvent *key;
  SDL_Scancode scancode;
  tme_keyboard_keyval_t keysym;
  struct tme_sdl_screen *screen;

  key = &event->key;
  scancode = key->keysym.scancode;
  if ((unsigned int) scancode >= SDL_NUM_SCANCODES) {
    return;
  }

  /* Cmd-Q (Ctrl-Q other than on macOS) quits, after asking, and
     isn't passed on: */
  if (key->keysym.sym == SDLK_q
      && (key->keysym.mod & TME_SDL_KMOD_PRIMARY)) {
    if (event->type == SDL_KEYDOWN && !key->repeat) {
      _tme_sdl_display_quit(display, _tme_sdl_screen_from_id(key->windowID));
    }
    return;
  }

  /* F10 opens the menus, and the menus take all keys while they're
     open.  the keys are released when the menus open: */
  screen = _tme_sdl_screen_from_id(key->windowID);
  if (screen != NULL
      && _tme_sdl_menu_key_event(screen, event)) {
    return;
  }

  /* turn mouse mode off if this completes the key combination for
     that: */
  _tme_sdl_mouse_key_event(display, key);

  /* a release sends the keysym that the press sent: */
  if (event->type == SDL_KEYUP) {
    keysym = display->tme_sdl_display_keys_down[scancode];
    display->tme_sdl_display_keys_down[scancode] = TME_KEYBOARD_KEYVAL_UNDEF;
    if (keysym != TME_KEYBOARD_KEYVAL_UNDEF) {
      _tme_keyboard_key_press(FALSE, keysym, &display->display);
    }
    return;
  }

  /* a press, or an autorepeat of one, which the keyboard buffer
     deals with: */
  keysym = display->tme_sdl_display_keys_down[scancode];
  if (keysym == TME_KEYBOARD_KEYVAL_UNDEF) {
    keysym = _tme_sdl_key_keysym(key);
    if (keysym == TME_KEYBOARD_KEYVAL_UNDEF
	|| keysym == TME_KEYSYM_VOID) {
      return;
    }
    display->tme_sdl_display_keys_down[scancode] = keysym;
  }
  _tme_keyboard_key_press(TRUE, keysym, &display->display);
}

/* this shows a screen's framebuffer, between the menu bar and the
   status line, and any open menus over it: */
void
_tme_sdl_screen_present(struct tme_sdl_screen *screen)
{
  SDL_Rect rect;

  if (screen->tme_sdl_screen_texture == NULL) {
    return;
  }
  SDL_SetRenderDrawColor(screen->tme_sdl_screen_renderer, 0, 0, 0, 255);
  SDL_RenderClear(screen->tme_sdl_screen_renderer);
  rect.x = 0;
  rect.y = screen->tme_sdl_screen_bar_height;
  rect.w = screen->tme_sdl_screen_texture_width;
  rect.h = screen->tme_sdl_screen_texture_height;
  SDL_RenderCopy(screen->tme_sdl_screen_renderer,
		 screen->tme_sdl_screen_texture,
		 NULL, &rect);
  _tme_sdl_menu_draw(screen);
  SDL_RenderPresent(screen->tme_sdl_screen_renderer);
}

/* this handles a window event: */
static void
_tme_sdl_window_event(struct tme_sdl_display *display,
		      const SDL_Event *event)
{
  struct tme_sdl_screen *screen;

  screen = _tme_sdl_screen_from_id(event->window.windowID);
  switch (event->window.event) {

  /* the window needs to be drawn again: */
  case SDL_WINDOWEVENT_EXPOSED:
    if (screen != NULL) {
      _tme_sdl_screen_present(screen);
    }
    break;

  /* when the window loses the focus, release the mouse, and the keys
     held down, whose releases it won't see: */
  case SDL_WINDOWEVENT_FOCUS_LOST:
    _tme_sdl_mouse_mode_off(display);
    _tme_sdl_keys_release(display);
    break;

  /* closing the window quits the emulator, after asking: */
  case SDL_WINDOWEVENT_CLOSE:
    _tme_sdl_display_quit(display, screen);
    break;

  default:
    break;
  }
}

/* the SDL display update function, called before the screens are
   updated: */
static int
_tme_sdl_display_update(struct tme_display *_display)
{
  struct tme_sdl_display *display;
  struct tme_screen *screen;
  struct tme_display_menus *menus;
  SDL_Event event;

  display = (struct tme_sdl_display *) _display;

  /* give new screens their menus and status lines: */
  for (screen = display->display.tme_display_screens;
       screen != NULL;
       screen = screen->tme_screen_next) {
    if (screen->tme_screen_menus == NULL) {
      _tme_display_menus_new(screen);
      _tme_sdl_display_mouse_hint(display);
    }
  }

  /* check on any file chooser: */
  _tme_sdl_files_poll(display);

  while (SDL_PollEvent(&event)) {
    switch (event.type) {

    case SDL_WINDOWEVENT:
      _tme_sdl_window_event(display, &event);
      break;

    case SDL_KEYDOWN:
    case SDL_KEYUP:
      _tme_sdl_key_event(display, &event);
      break;

    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      _tme_sdl_mouse_event(display, &event);
      break;

    /* on macOS, Cmd-Q from the application menu: */
    case SDL_QUIT:
      _tme_sdl_display_quit(display, NULL);
      break;

    /* text that wasn't for a key press: */
    case SDL_TEXTINPUT:
    default:
      break;
    }
  }

  /* draw the menus and status lines again if they have changed: */
  for (screen = display->display.tme_display_screens;
       screen != NULL;
       screen = screen->tme_screen_next) {
    menus = screen->tme_screen_menus;
    if (menus->tme_display_menus_generation
	!= ((struct tme_sdl_screen *) screen)->tme_sdl_screen_menus_drawn
	|| menus->tme_display_menus_status_generation
	!= ((struct tme_sdl_screen *) screen)->tme_sdl_screen_status_drawn) {
      _tme_sdl_screen_present((struct tme_sdl_screen *) screen);
    }
  }
  return (TME_OK);
}

/* the bell, a short beep, or if there is no sound, a flash of the
   window: */
static int
_tme_sdl_display_bell(struct tme_display *_display)
{
  struct tme_sdl_display *display;
  SDL_AudioSpec want;
  static Sint16 samples[TME_SDL_BELL_RATE * TME_SDL_BELL_MSEC / 1000];
  unsigned int sample_i, period;
  struct tme_screen *screen;

  display = (struct tme_sdl_display *) _display;

  /* open the audio device the first time: */
  if (display->tme_sdl_display_bell_device == 0
      && !display->tme_sdl_display_bell_failed) {
    memset(&want, 0, sizeof(want));
    want.freq = TME_SDL_BELL_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = NULL;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0
	|| (display->tme_sdl_display_bell_device
	    = SDL_OpenAudioDevice(NULL, FALSE, &want, NULL, 0)) == 0) {
      display->tme_sdl_display_bell_failed = TRUE;
    }
    else {
      SDL_PauseAudioDevice(display->tme_sdl_display_bell_device, 0);
    }
  }

  /* beep, unless the last beep is still going: */
  if (display->tme_sdl_display_bell_device != 0) {
    if (SDL_GetQueuedAudioSize(display->tme_sdl_display_bell_device) == 0) {
      period = TME_SDL_BELL_RATE / TME_SDL_BELL_HZ;
      for (sample_i = 0; sample_i < TME_ARRAY_ELS(samples); sample_i++) {
	samples[sample_i] = ((sample_i % period) < (period / 2)
			     ? TME_SDL_BELL_VOLUME
			     : -TME_SDL_BELL_VOLUME);
      }
      SDL_QueueAudio(display->tme_sdl_display_bell_device,
		     samples, sizeof(samples));
    }
    return (TME_OK);
  }

  for (screen = display->display.tme_display_screens;
       screen != NULL;
       screen = screen->tme_screen_next) {
    SDL_FlashWindow(((struct tme_sdl_screen *) screen)->tme_sdl_screen_window,
		    SDL_FLASH_BRIEFLY);
  }
  return (TME_OK);
}

/* this is called with the mutex locked when a screen's framebuffer
   size has changed: */
static int
_tme_sdl_screen_resize(struct tme_screen *_screen)
{
  struct tme_sdl_screen *screen;
  struct tme_display *display;
  struct tme_fb_connection *conn_fb;
  const struct tme_fb_connection *conn_fb_other;
  int scale, width, height, height_visible, device_scale;
  size_t size;

  screen = (struct tme_sdl_screen *) _screen;
  display = screen->screen.tme_screen_display;
  conn_fb = screen->screen.tme_screen_fb;
  conn_fb_other = (const struct tme_fb_connection *) conn_fb->tme_fb_connection.tme_connection_other;

  /* the framebuffer translation writes the width and height that
     _tme_screen_configure picked, which has a scanline or two more
     than are shown: */
  width = conn_fb->tme_fb_connection_width;
  height = conn_fb->tme_fb_connection_height;
  scale = screen->screen.tme_screen_fb_scale;
  if (scale < 0) {
    scale = -scale;
  }
  height_visible = (conn_fb_other->tme_fb_connection_height * scale) / TME_FB_XLAT_SCALE_NONE;

  /* (re)allocate the pixels: */
  size = (size_t) width * height * sizeof(tme_uint32_t);
  if (size != screen->tme_sdl_screen_pixels_size) {
    if (screen->tme_sdl_screen_pixels != NULL) {
      tme_free(screen->tme_sdl_screen_pixels);
    }
    screen->tme_sdl_screen_pixels = tme_malloc0(size);
    screen->tme_sdl_screen_pixels_size = size;
  }

  /* update our framebuffer connection: */
  conn_fb->tme_fb_connection_skipx = 0;
  conn_fb->tme_fb_connection_scanline_pad = _tme_scanline_pad(width * sizeof(tme_uint32_t));
  conn_fb->tme_fb_connection_order = TME_ENDIAN_NATIVE;
  conn_fb->tme_fb_connection_buffer = (tme_uint8_t *) screen->tme_sdl_screen_pixels;
  conn_fb->tme_fb_connection_buffsz = size;
  conn_fb->tme_fb_connection_bits_per_pixel = 32;
  conn_fb->tme_fb_connection_depth = 24;
  conn_fb->tme_fb_connection_class = TME_FB_XLAT_CLASS_COLOR;
  conn_fb->tme_fb_connection_mask_g = 0x00ff00;
  conn_fb->tme_fb_connection_mask_b = 0x0000ff;
  conn_fb->tme_fb_connection_mask_r = 0xff0000;

  /* the translation only writes what changed since it last ran, so
     make the next one start over and draw all of the new pixels: */
  screen->screen.tme_screen_fb_xlat = NULL;

  /* (re)create the texture: */
  if (screen->tme_sdl_screen_texture == NULL
      || screen->tme_sdl_screen_texture_width != width
      || screen->tme_sdl_screen_texture_height != height_visible) {
    if (screen->tme_sdl_screen_texture != NULL) {
      SDL_DestroyTexture(screen->tme_sdl_screen_texture);
    }
    screen->tme_sdl_screen_texture
      = SDL_CreateTexture(screen->tme_sdl_screen_renderer,
			  SDL_PIXELFORMAT_RGB888,
			  SDL_TEXTUREACCESS_STREAMING,
			  width, height_visible);
    if (screen->tme_sdl_screen_texture == NULL) {
      _tme_sdl_log_error(display, _("cannot create the screen texture"));
    }
    screen->tme_sdl_screen_texture_width = width;
    screen->tme_sdl_screen_texture_height = height_visible;
  }

  /* the framebuffer is in device pixels, and the window size is in
     logical ones.  the window also has the menu bar and the status
     line: */
  device_scale = screen->screen.tme_screen_scale;
  SDL_SetWindowSize(screen->tme_sdl_screen_window,
		    (width + device_scale - 1) / device_scale,
		    (height_visible + 2 * screen->tme_sdl_screen_bar_height
		     + device_scale - 1) / device_scale);
  SDL_ShowWindow(screen->tme_sdl_screen_window);
  return (TME_OK);
}

/* this is called with the mutex locked when a screen's framebuffer
   has changed: */
static int
_tme_sdl_screen_redraw(struct tme_screen *_screen, int x, int y, int w, int h)
{
  struct tme_sdl_screen *screen;

  screen = (struct tme_sdl_screen *) _screen;
  if (screen->tme_sdl_screen_texture == NULL
      || screen->tme_sdl_screen_pixels == NULL) {
    return (TME_OK);
  }
  SDL_UpdateTexture(screen->tme_sdl_screen_texture, NULL,
		    screen->tme_sdl_screen_pixels,
		    screen->tme_sdl_screen_texture_width * sizeof(tme_uint32_t));
  _tme_sdl_screen_present(screen);
  return (TME_OK);
}

/* this captures an image of a screen, at the size of the emulated
   framebuffer, with the mutex locked: */
static int
_tme_sdl_screen_capture(struct tme_screen *_screen,
			struct tme_display_image *image)
{
  struct tme_sdl_screen *screen;
  const struct tme_fb_connection *conn_fb_other;
  const tme_uint32_t *row;
  tme_uint32_t *pixel;
  int width, height, x, y;

  screen = (struct tme_sdl_screen *) _screen;
  conn_fb_other
    = (screen->screen.tme_screen_fb != NULL
       ? ((const struct tme_fb_connection *)
	  screen->screen.tme_screen_fb->tme_fb_connection.tme_connection_other)
       : NULL);
  if (screen->tme_sdl_screen_pixels == NULL
      || screen->tme_sdl_screen_texture_width <= 0
      || screen->tme_sdl_screen_texture_height <= 0
      || conn_fb_other == NULL
      || conn_fb_other->tme_fb_connection_width <= 0
      || conn_fb_other->tme_fb_connection_height <= 0) {
    return (EAGAIN);
  }

  /* the pixels are scaled from the emulated framebuffer, so take
     every scaled pixel's top left device pixel: */
  width = conn_fb_other->tme_fb_connection_width;
  height = conn_fb_other->tme_fb_connection_height;
  image->tme_display_image_width = width;
  image->tme_display_image_height = height;
  image->tme_display_image_pixels = tme_new(tme_uint32_t, (size_t) width * height);
  pixel = image->tme_display_image_pixels;
  for (y = 0; y < height; y++) {
    row = (screen->tme_sdl_screen_pixels
	   + ((size_t) ((y * screen->tme_sdl_screen_texture_height) / height)
	      * screen->tme_sdl_screen_texture_width));
    for (x = 0; x < width; x++) {
      *(pixel++) = row[(x * screen->tme_sdl_screen_texture_width) / width] & 0xffffff;
    }
  }
  return (TME_OK);
}

/* this makes a new screen, with the mutex locked: */
static struct tme_screen *
_tme_sdl_screen_new(struct tme_display *_display,
		    struct tme_connection *conn)
{
  struct tme_sdl_screen *screen;
  int window_width, output_width, output_height;

  screen = tme_screen_new(_display, struct tme_sdl_screen, conn);

  /* make the window, hidden until it has its size, and its renderer.
     on HiDPI screens the window gets the screen's full resolution: */
  screen->tme_sdl_screen_window
    = SDL_CreateWindow(_display->tme_display_title,
		       SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		       BLANK_SIDE, BLANK_SIDE,
		       SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);
  if (screen->tme_sdl_screen_window == NULL) {
    _tme_sdl_log_error(_display, _("cannot create the window"));
    abort();
  }
  SDL_SetWindowData(screen->tme_sdl_screen_window, "tme", screen);
  _tme_sdl_set_icon(screen->tme_sdl_screen_window);
  screen->tme_sdl_screen_renderer
    = SDL_CreateRenderer(screen->tme_sdl_screen_window, -1, 0);
  if (screen->tme_sdl_screen_renderer == NULL) {
    screen->tme_sdl_screen_renderer
      = SDL_CreateRenderer(screen->tme_sdl_screen_window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (screen->tme_sdl_screen_renderer == NULL) {
    _tme_sdl_log_error(_display, _("cannot create the renderer"));
    abort();
  }
  SDL_SetRenderDrawColor(screen->tme_sdl_screen_renderer, 0, 0, 0, 255);

  /* the device scale is how many pixels the renderer has for each
     logical pixel of the window: */
  SDL_GetWindowSize(screen->tme_sdl_screen_window, &window_width, NULL);
  if (SDL_GetRendererOutputSize(screen->tme_sdl_screen_renderer,
				&output_width, &output_height) == 0
      && window_width > 0
      && output_width >= window_width) {
    screen->screen.tme_screen_scale = output_width / window_width;
  }

  /* make the font for the menus and the status line, for the device
     scale: */
  _tme_sdl_menu_init(screen);
  return (&screen->screen);
}

/* the new SDL display function: */
TME_ELEMENT_SUB_NEW_DECL(tme_host_sdl,display) {
  struct tme_sdl_display *display;
  SDL_Rect bounds;
  unsigned int scancode;

  /* we take no arguments: */
  if (args[1] != NULL) {
    tme_output_append_error(_output, "%s %s", args[1], _("unexpected"));
    return (EINVAL);
  }

#ifdef TME_SDL_THREADS_UNSUPPORTED
  tme_output_append_error(_output,
			  _("on macOS, the SDL display needs tme configured without preemptive threads"));
  return (ENOTSUP);
#endif

  /* SDL turns SIGINT into a quit event, but leave it to kill tmesh: */
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  /* the app ID that matches tme.desktop, for its icon in task switchers: */
  SDL_SetHint("SDL_VIDEO_X11_WMCLASS", "tmesh");
  SDL_SetHint("SDL_VIDEO_WAYLAND_WMCLASS", "tmesh");

  /* a click on the window when it doesn't have the focus also goes to
     the window, to turn on mouse mode: */
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

  /* closing the last window asks whether to quit, instead of also
     quitting: */
#ifdef SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE
  SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
#endif

  /* scale the framebuffer without smoothing: */
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    tme_output_append_error(_output, _("cannot start SDL: %s"), SDL_GetError());
    return (ENXIO);
  }
  atexit(SDL_Quit);
  SDL_StartTextInput();

  /* start our data structure: */
  display = tme_new0(struct tme_sdl_display, 1);
  for (scancode = 0; scancode < SDL_NUM_SCANCODES; scancode++) {
    display->tme_sdl_display_keys_down[scancode] = TME_KEYBOARD_KEYVAL_UNDEF;
  }
  tme_display_init(element, &display->display);

  /* the size of the screen, for picking the default scaling: */
  if (SDL_GetDisplayUsableBounds(0, &bounds) == 0
      && bounds.w > 0
      && bounds.h > 0) {
    display->display.tme_screen_width = bounds.w;
    display->display.tme_screen_height = TME_MAX(bounds.h - TME_SDL_BARS_HEIGHT, 1);
  }

  /* set the display-specific functions: */
  display->display.tme_display_bell = _tme_sdl_display_bell;
  display->display.tme_display_update = _tme_sdl_display_update;
  display->display.tme_screen_add = _tme_sdl_screen_new;
  display->display.tme_screen_resize = _tme_sdl_screen_resize;
  display->display.tme_screen_redraw = _tme_sdl_screen_redraw;
  display->display.tme_screen_quit = _tme_sdl_screen_quit;
  display->display.tme_screen_error = _tme_sdl_screen_error;
  display->display.tme_screen_capture = _tme_sdl_screen_capture;
  _tme_sdl_files_init(display);

  return (TME_OK);
}
