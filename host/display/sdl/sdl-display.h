/* host/display/sdl/sdl-display.h - header file for SDL2 display support: */

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

#ifndef _HOST_SDL_DISPLAY_H
#define _HOST_SDL_DISPLAY_H

#include <tme/common.h>

/* includes: */
#include "display.h"
#include <SDL.h>
#include <sys/types.h>

/* macros: */

/* how deep menus can be open: */
#define TME_SDL_MENU_DEPTH		(4)

/* types: */

/* a running file chooser: */
struct tme_sdl_files {

  /* the chooser process, and the pipe from it: */
  pid_t tme_sdl_files_pid;
  int tme_sdl_files_fd;

  /* what it has printed so far: */
  char *tme_sdl_files_output;
  size_t tme_sdl_files_output_length;

  /* the screen and the request: */
  struct tme_screen *tme_sdl_files_screen;
  struct tme_display_files_request *tme_sdl_files_request;
};

/* a display: */
struct tme_sdl_display {

  /* the generic display structure: */
  struct tme_display display;

  /* nonzero iff mouse mode is on, and the pointer is captured: */
  int tme_sdl_display_mouse_on;

  /* the modifier keys being held down, for the key combination
     that turns mouse mode off: */
  unsigned int tme_sdl_display_mouse_mods;

  /* the keysym that each key pressed down sent, so that its release
     sends the same keysym, or TME_KEYBOARD_KEYVAL_UNDEF: */
  tme_keyboard_keyval_t tme_sdl_display_keys_down[SDL_NUM_SCANCODES];

  /* the audio device for the bell, if it has been opened: */
  SDL_AudioDeviceID tme_sdl_display_bell_device;
  int tme_sdl_display_bell_failed;

  /* any running file chooser: */
  struct tme_sdl_files *tme_sdl_display_files;
};

/* a screen: */
struct tme_sdl_screen {

  /* the generic screen structure: */
  struct tme_screen screen;

  /* the window, its renderer, and the texture that holds the
     framebuffer: */
  SDL_Window *tme_sdl_screen_window;
  SDL_Renderer *tme_sdl_screen_renderer;
  SDL_Texture *tme_sdl_screen_texture;
  int tme_sdl_screen_texture_width;
  int tme_sdl_screen_texture_height;

  /* the framebuffer translation writes here: */
  tme_uint32_t *tme_sdl_screen_pixels;
  size_t tme_sdl_screen_pixels_size;

  /* the font texture, and the size of a character, in pixels: */
  SDL_Texture *tme_sdl_screen_font;
  int tme_sdl_screen_font_width;
  int tme_sdl_screen_font_height;

  /* the space around text, and the height of the menu bar and of
     the status line, in pixels: */
  int tme_sdl_screen_pad;
  int tme_sdl_screen_bar_height;

  /* the menu and status generations last drawn: */
  unsigned long tme_sdl_screen_menus_drawn;
  unsigned long tme_sdl_screen_status_drawn;

  /* the open menus: how many are open, the index of the item that
     opened each one (a menu bar item for the first), the index of
     the highlighted item in each one, or -1, and nonzero if the
     mouse button press now down opened the menus: */
  int tme_sdl_screen_menu_depth;
  int tme_sdl_screen_menu_opener[TME_SDL_MENU_DEPTH];
  int tme_sdl_screen_menu_highlight[TME_SDL_MENU_DEPTH];
  int tme_sdl_screen_menu_press_opened;
};

/* this returns the display for a screen: */
#define _tme_sdl_screen_display(screen) \
  ((struct tme_sdl_display *) (screen)->screen.tme_screen_display)

/* prototypes: */
void _tme_sdl_screen_present _TME_P((struct tme_sdl_screen *));
void _tme_sdl_keys_release _TME_P((struct tme_sdl_display *));
void _tme_sdl_mouse_mode_off _TME_P((struct tme_sdl_display *));
void _tme_sdl_menu_init _TME_P((struct tme_sdl_screen *));
void _tme_sdl_menu_draw _TME_P((struct tme_sdl_screen *));
int _tme_sdl_menu_mouse_event _TME_P((struct tme_sdl_screen *, const SDL_Event *));
int _tme_sdl_menu_key_event _TME_P((struct tme_sdl_screen *, const SDL_Event *));
int _tme_sdl_menu_open _TME_P((struct tme_sdl_screen *));
int _tme_sdl_files_init _TME_P((struct tme_sdl_display *));
void _tme_sdl_files_poll _TME_P((struct tme_sdl_display *));

#endif /* _HOST_SDL_DISPLAY_H */
