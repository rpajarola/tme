/* host/display/sdl/sdl-menu.c - SDL2 menu bar and status line: */

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

/* the SDL display draws the menus and the status line from
   display-menu.c itself: the menu bar above the framebuffer, the
   status line below it, and any open menus over it.  the menus work
   with the mouse when mouse mode is off, and with the keyboard after
   F10: */

#include <tme/common.h>

/* includes: */
#include "sdl-display.h"
#include "sdl-font.h"
#include <string.h>

/* the colors: */
#define TME_SDL_COLOR_BAR		(0xe4e4e4)
#define TME_SDL_COLOR_MENU		(0xf6f6f6)
#define TME_SDL_COLOR_TEXT		(0x000000)
#define TME_SDL_COLOR_DISABLED		(0x8c8c8c)
#define TME_SDL_COLOR_HIGHLIGHT		(0x2f5fb3)
#define TME_SDL_COLOR_HIGHLIGHT_TEXT	(0xffffff)
#define TME_SDL_COLOR_BORDER		(0x8a8a8a)
#define TME_SDL_COLOR_SEPARATOR		(0xc8c8c8)

/* the character that shows that an item opens a submenu, a right
   guillemet in UTF-8: */
#define TME_SDL_MENU_ARROW		"\302\273"

/* this sets the draw color: */
static void
_tme_sdl_color(SDL_Renderer *renderer, Uint32 rgb)
{
  SDL_SetRenderDrawColor(renderer,
			 (rgb >> 16) & 0xff,
			 (rgb >> 8) & 0xff,
			 rgb & 0xff,
			 0xff);
}

/* this fills a rectangle: */
static void
_tme_sdl_fill(struct tme_sdl_screen *screen,
	      int x, int y, int w, int h,
	      Uint32 rgb)
{
  SDL_Rect rect;

  rect.x = x;
  rect.y = y;
  rect.w = w;
  rect.h = h;
  _tme_sdl_color(screen->tme_sdl_screen_renderer, rgb);
  SDL_RenderFillRect(screen->tme_sdl_screen_renderer, &rect);
}

/* this makes the font texture and sets the sizes of things, for the
   screen's device scale.  on HiDPI screens the larger font is used,
   instead of scaling the smaller one up: */
void
_tme_sdl_menu_init(struct tme_sdl_screen *screen)
{
  SDL_Surface *surface;
  const unsigned char *glyph;
  Uint32 *pixels;
  int scale, zoom, width, height, bytes, c, row, col;

  scale = screen->screen.tme_screen_scale;
  if (scale < 1) {
    scale = 1;
  }
  if (scale >= 2) {
    width = TME_SDL_FONT_16X32_WIDTH;
    height = TME_SDL_FONT_16X32_HEIGHT;
    zoom = scale / 2;
  }
  else {
    width = TME_SDL_FONT_8X16_WIDTH;
    height = TME_SDL_FONT_8X16_HEIGHT;
    zoom = 1;
  }
  bytes = (width + 7) / 8;

  /* the font texture has the characters in rows of 16, white where
     they're drawn and transparent elsewhere, so they can be drawn in
     any color: */
  if (screen->tme_sdl_screen_font != NULL) {
    SDL_DestroyTexture(screen->tme_sdl_screen_font);
    screen->tme_sdl_screen_font = NULL;
  }
  surface = SDL_CreateRGBSurfaceWithFormat(0, 16 * width, 14 * height,
					   32, SDL_PIXELFORMAT_ARGB8888);
  if (surface != NULL) {
    SDL_LockSurface(surface);
    for (c = TME_SDL_FONT_FIRST; c <= TME_SDL_FONT_LAST; c++) {
      glyph = (width == TME_SDL_FONT_16X32_WIDTH
	       ? _tme_sdl_font_16x32[c - TME_SDL_FONT_FIRST]
	       : _tme_sdl_font_8x16[c - TME_SDL_FONT_FIRST]);
      for (row = 0; row < height; row++) {
	pixels = (Uint32 *) ((Uint8 *) surface->pixels
			     + (((c - TME_SDL_FONT_FIRST) / 16) * height + row) * surface->pitch);
	pixels += ((c - TME_SDL_FONT_FIRST) % 16) * width;
	for (col = 0; col < width; col++) {
	  pixels[col] = ((glyph[row * bytes + col / 8] & (0x80 >> (col % 8)))
			 ? 0xffffffff
			 : 0x00ffffff);
	}
      }
    }
    SDL_UnlockSurface(surface);
    screen->tme_sdl_screen_font
      = SDL_CreateTextureFromSurface(screen->tme_sdl_screen_renderer, surface);
    SDL_FreeSurface(surface);
  }
  if (screen->tme_sdl_screen_font != NULL) {
    SDL_SetTextureBlendMode(screen->tme_sdl_screen_font, SDL_BLENDMODE_BLEND);
  }

  screen->tme_sdl_screen_font_width = width * zoom;
  screen->tme_sdl_screen_font_height = height * zoom;
  screen->tme_sdl_screen_pad = 3 * scale;
  screen->tme_sdl_screen_bar_height
    = screen->tme_sdl_screen_font_height + 2 * screen->tme_sdl_screen_pad;
  screen->tme_sdl_screen_bar_height
    += (scale - (screen->tme_sdl_screen_bar_height % scale)) % scale;
}

/* this returns the next character in UTF-8 text, as Latin-1.  other
   characters are '?', and bytes that aren't UTF-8 are taken as
   Latin-1: */
static int
_tme_sdl_text_next(const char **_text)
{
  const unsigned char *s;
  unsigned long c;
  int more, i;

  s = (const unsigned char *) *_text;
  if (s[0] < 0x80) {
    *_text += 1;
    return (s[0]);
  }
  if ((s[0] & 0xe0) == 0xc0) {
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
    *_text += 1;
    return (s[0]);
  }
  for (i = 1; i <= more; i++) {
    if ((s[i] & 0xc0) != 0x80) {
      *_text += 1;
      return (s[0]);
    }
    c = (c << 6) | (s[i] & 0x3f);
  }
  *_text += 1 + more;
  return (c <= 0xff ? (int) c : '?');
}

/* this returns the number of characters in UTF-8 text: */
static int
_tme_sdl_text_length(const char *text)
{
  int length;

  if (text == NULL) {
    return (0);
  }
  for (length = 0; *text != '\0'; length++) {
    _tme_sdl_text_next(&text);
  }
  return (length);
}

/* this draws one character: */
static void
_tme_sdl_char_draw(struct tme_sdl_screen *screen, int x, int y, int c)
{
  SDL_Rect src, dst;
  int width, height;

  if (c < TME_SDL_FONT_FIRST || c > TME_SDL_FONT_LAST) {
    c = '?';
  }
  width = (screen->tme_sdl_screen_font_width >= TME_SDL_FONT_16X32_WIDTH
	   ? TME_SDL_FONT_16X32_WIDTH
	   : TME_SDL_FONT_8X16_WIDTH);
  height = (width == TME_SDL_FONT_16X32_WIDTH
	    ? TME_SDL_FONT_16X32_HEIGHT
	    : TME_SDL_FONT_8X16_HEIGHT);
  src.x = ((c - TME_SDL_FONT_FIRST) % 16) * width;
  src.y = ((c - TME_SDL_FONT_FIRST) / 16) * height;
  src.w = width;
  src.h = height;
  dst.x = x;
  dst.y = y;
  dst.w = screen->tme_sdl_screen_font_width;
  dst.h = screen->tme_sdl_screen_font_height;
  SDL_RenderCopy(screen->tme_sdl_screen_renderer,
		 screen->tme_sdl_screen_font,
		 &src, &dst);
}

/* this draws UTF-8 text, at most some number of characters of it.
   text that is too long ends in "...": */
static void
_tme_sdl_text_draw(struct tme_sdl_screen *screen,
		   int x, int y,
		   const char *text,
		   Uint32 rgb,
		   int max)
{
  int length, count;

  if (text == NULL
      || max <= 0
      || screen->tme_sdl_screen_font == NULL) {
    return;
  }
  SDL_SetTextureColorMod(screen->tme_sdl_screen_font,
			 (rgb >> 16) & 0xff,
			 (rgb >> 8) & 0xff,
			 rgb & 0xff);
  length = _tme_sdl_text_length(text);
  count = (length <= max ? length : max - 3);
  for (; count > 0 && *text != '\0'; count--) {
    _tme_sdl_char_draw(screen, x, y, _tme_sdl_text_next(&text));
    x += screen->tme_sdl_screen_font_width;
  }
  if (length > max) {
    for (count = TME_MIN(max, 3); count > 0; count--) {
      _tme_sdl_char_draw(screen, x, y, '.');
      x += screen->tme_sdl_screen_font_width;
    }
  }
}

/* this returns the size of the renderer's output, in pixels: */
static void
_tme_sdl_output_size(struct tme_sdl_screen *screen, int *_width, int *_height)
{
  if (SDL_GetRendererOutputSize(screen->tme_sdl_screen_renderer,
				_width, _height) != 0) {
    *_width = 0;
    *_height = 0;
  }
}

/* this returns the item at an index in a menu, or NULL: */
static struct tme_display_menu_item *
_tme_sdl_menu_nth(const struct tme_display_menu *menu, int index)
{
  struct tme_display_menu_item *item;

  if (menu == NULL || index < 0) {
    return (NULL);
  }
  for (item = menu->tme_display_menu_items;
       item != NULL && index > 0;
       item = item->tme_display_menu_item_next, index--);
  return (item);
}

/* this returns the menu bar, or NULL: */
static struct tme_display_menu *
_tme_sdl_menu_bar(struct tme_sdl_screen *screen)
{
  return (screen->screen.tme_screen_menus == NULL
	  ? NULL
	  : screen->screen.tme_screen_menus->tme_display_menus_bar);
}

/* this returns the menu open at a level, or NULL: */
static struct tme_display_menu *
_tme_sdl_menu_level(struct tme_sdl_screen *screen, int level)
{
  struct tme_display_menu *menu;
  struct tme_display_menu_item *item;
  int level_i;

  menu = _tme_sdl_menu_bar(screen);
  for (level_i = 0; level_i <= level; level_i++) {
    item = _tme_sdl_menu_nth(menu, screen->tme_sdl_screen_menu_opener[level_i]);
    if (item == NULL
	|| item->tme_display_menu_item_type != TME_DISPLAY_MENU_ITEM_SUBMENU) {
      return (NULL);
    }
    menu = item->tme_display_menu_item_submenu;
  }
  return (menu);
}

/* this makes sure that the open menus still exist, since activating
   an item can rebuild menus: */
static void
_tme_sdl_menu_check(struct tme_sdl_screen *screen)
{
  int level;

  for (level = 0; level < screen->tme_sdl_screen_menu_depth; level++) {
    if (_tme_sdl_menu_level(screen, level) == NULL) {
      break;
    }
  }
  screen->tme_sdl_screen_menu_depth = level;
}

/* this returns the height of a menu item, in pixels: */
static int
_tme_sdl_menu_item_height(struct tme_sdl_screen *screen,
			  const struct tme_display_menu_item *item)
{
  return (item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_SEPARATOR
	  ? screen->tme_sdl_screen_pad * 2
	  : screen->tme_sdl_screen_font_height + screen->tme_sdl_screen_pad);
}

/* this returns the width of a border, in pixels: */
static int
_tme_sdl_menu_border(struct tme_sdl_screen *screen)
{
  return (TME_MAX(screen->screen.tme_screen_scale, 1));
}

/* this returns the x coordinate and width of a menu bar item: */
static void
_tme_sdl_menu_bar_item_x(struct tme_sdl_screen *screen,
			 int index,
			 int *_x, int *_width)
{
  struct tme_display_menu_item *item;
  int x;

  x = screen->tme_sdl_screen_font_width / 2;
  for (item = _tme_sdl_menu_bar(screen)->tme_display_menu_items;
       item != NULL;
       item = item->tme_display_menu_item_next, index--) {
    *_width = (_tme_sdl_text_length(item->tme_display_menu_item_label) + 2) * screen->tme_sdl_screen_font_width;
    if (index == 0) {
      break;
    }
    x += *_width;
  }
  *_x = x;
}

/* this returns the menu bar item at a point, or -1: */
static int
_tme_sdl_menu_bar_hit(struct tme_sdl_screen *screen, int x, int y)
{
  struct tme_display_menu_item *item;
  int index, item_x, item_width;

  if (_tme_sdl_menu_bar(screen) == NULL
      || y < 0
      || y >= screen->tme_sdl_screen_bar_height) {
    return (-1);
  }
  for (index = 0, item = _tme_sdl_menu_bar(screen)->tme_display_menu_items;
       item != NULL;
       index++, item = item->tme_display_menu_item_next) {
    _tme_sdl_menu_bar_item_x(screen, index, &item_x, &item_width);
    if (x >= item_x && x < item_x + item_width) {
      return (index);
    }
  }
  return (-1);
}

/* this returns the rectangle of the menu open at a level: */
static void
_tme_sdl_menu_rect(struct tme_sdl_screen *screen, int level, SDL_Rect *rect)
{
  const struct tme_display_menu *menu;
  const struct tme_display_menu_item *item;
  SDL_Rect parent;
  int label, shortcut, arrow, length, border, output_width, output_height, index;

  menu = _tme_sdl_menu_level(screen, level);
  border = _tme_sdl_menu_border(screen);

  /* the width has room for a check mark, the longest label, the
     longest shortcut, and a submenu arrow: */
  label = 0;
  shortcut = 0;
  arrow = 0;
  rect->h = 2 * border + screen->tme_sdl_screen_pad;
  for (item = menu->tme_display_menu_items;
       item != NULL;
       item = item->tme_display_menu_item_next) {
    length = _tme_sdl_text_length(item->tme_display_menu_item_label);
    label = TME_MAX(label, length);
    length = _tme_sdl_text_length(item->tme_display_menu_item_shortcut);
    shortcut = TME_MAX(shortcut, length);
    if (item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_SUBMENU) {
      arrow = 2;
    }
    rect->h += _tme_sdl_menu_item_height(screen, item);
  }
  rect->w = (2 + label + (shortcut > 0 ? 3 + shortcut : 0) + (arrow > 0 ? 1 + arrow : 0) + 1) * screen->tme_sdl_screen_font_width;
  rect->w += 2 * border;

  /* the first menu is below its menu bar item, and a submenu is
     next to the item that opened it: */
  _tme_sdl_output_size(screen, &output_width, &output_height);
  if (level == 0) {
    _tme_sdl_menu_bar_item_x(screen, screen->tme_sdl_screen_menu_opener[0], &rect->x, &length);
    rect->y = screen->tme_sdl_screen_bar_height;
  }
  else {
    _tme_sdl_menu_rect(screen, level - 1, &parent);
    rect->x = parent.x + parent.w - border;
    if (rect->x + rect->w > output_width) {
      rect->x = parent.x - rect->w + border;
    }
    rect->y = parent.y + border + screen->tme_sdl_screen_pad / 2 - border;
    menu = _tme_sdl_menu_level(screen, level - 1);
    for (index = 0, item = menu->tme_display_menu_items;
	 item != NULL && index < screen->tme_sdl_screen_menu_opener[level];
	 index++, item = item->tme_display_menu_item_next) {
      rect->y += _tme_sdl_menu_item_height(screen, item);
    }
  }

  /* keep the menu inside the window: */
  if (rect->x + rect->w > output_width) {
    rect->x = output_width - rect->w;
  }
  if (rect->x < 0) {
    rect->x = 0;
  }
  if (rect->y + rect->h > output_height) {
    rect->y = output_height - rect->h;
  }
  if (rect->y < 0) {
    rect->y = 0;
  }
}

/* this returns the open menu level and item at a point.  the level is
   -1 if the point isn't in an open menu, and the item is -1 if the
   point isn't on an item: */
static int
_tme_sdl_menu_hit(struct tme_sdl_screen *screen, int x, int y, int *_index)
{
  const struct tme_display_menu_item *item;
  SDL_Rect rect;
  int level, index, item_y, height;

  *_index = -1;
  for (level = screen->tme_sdl_screen_menu_depth; --level >= 0; ) {
    _tme_sdl_menu_rect(screen, level, &rect);
    if (x < rect.x || x >= rect.x + rect.w
	|| y < rect.y || y >= rect.y + rect.h) {
      continue;
    }
    item_y = rect.y + _tme_sdl_menu_border(screen) + screen->tme_sdl_screen_pad / 2;
    for (index = 0, item = _tme_sdl_menu_level(screen, level)->tme_display_menu_items;
	 item != NULL;
	 index++, item = item->tme_display_menu_item_next) {
      height = _tme_sdl_menu_item_height(screen, item);
      if (y >= item_y && y < item_y + height) {
	if (item->tme_display_menu_item_type != TME_DISPLAY_MENU_ITEM_SEPARATOR) {
	  *_index = index;
	}
	break;
      }
      item_y += height;
    }
    return (level);
  }
  return (-1);
}

/* this draws the menu open at a level: */
static void
_tme_sdl_menu_draw_level(struct tme_sdl_screen *screen, int level)
{
  const struct tme_display_menu_item *item;
  SDL_Rect rect;
  Uint32 text;
  int border, fw, fh, pad, index, y, height, highlight, columns, length, mark;

  _tme_sdl_menu_rect(screen, level, &rect);
  border = _tme_sdl_menu_border(screen);
  fw = screen->tme_sdl_screen_font_width;
  fh = screen->tme_sdl_screen_font_height;
  pad = screen->tme_sdl_screen_pad;
  columns = (rect.w - 2 * border) / fw;

  _tme_sdl_fill(screen, rect.x, rect.y, rect.w, rect.h, TME_SDL_COLOR_BORDER);
  _tme_sdl_fill(screen, rect.x + border, rect.y + border,
		rect.w - 2 * border, rect.h - 2 * border,
		TME_SDL_COLOR_MENU);

  y = rect.y + border + pad / 2;
  for (index = 0, item = _tme_sdl_menu_level(screen, level)->tme_display_menu_items;
       item != NULL;
       index++, item = item->tme_display_menu_item_next) {
    height = _tme_sdl_menu_item_height(screen, item);

    /* a separator is a line: */
    if (item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_SEPARATOR) {
      _tme_sdl_fill(screen, rect.x + border + fw / 2, y + height / 2,
		    rect.w - 2 * border - fw, border,
		    TME_SDL_COLOR_SEPARATOR);
      y += height;
      continue;
    }

    /* the highlighted item, or the item whose submenu is open, is in
       the highlight colors, if it's enabled: */
    highlight = (item->tme_display_menu_item_enabled
		 && (index == screen->tme_sdl_screen_menu_highlight[level]
		     || (level + 1 < screen->tme_sdl_screen_menu_depth
			 && index == screen->tme_sdl_screen_menu_opener[level + 1])));
    if (highlight) {
      _tme_sdl_fill(screen, rect.x + border, y, rect.w - 2 * border, height,
		    TME_SDL_COLOR_HIGHLIGHT);
      text = TME_SDL_COLOR_HIGHLIGHT_TEXT;
    }
    else {
      text = (item->tme_display_menu_item_enabled
	      ? TME_SDL_COLOR_TEXT
	      : TME_SDL_COLOR_DISABLED);
    }

    /* a checked item has a mark: */
    if ((item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_CHECK
	 || item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_RADIO)
	&& item->tme_display_menu_item_checked) {
      mark = fw / 2;
      _tme_sdl_fill(screen,
		    rect.x + border + fw - mark / 2,
		    y + (height - mark) / 2,
		    mark, mark,
		    text);
    }

    /* the label, and any shortcut and submenu arrow at the right: */
    _tme_sdl_text_draw(screen, rect.x + border + 2 * fw, y + pad / 2,
		       item->tme_display_menu_item_label, text, columns - 3);
    if (item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_SUBMENU) {
      _tme_sdl_text_draw(screen, rect.x + rect.w - border - 2 * fw, y + pad / 2,
			 TME_SDL_MENU_ARROW, text, 1);
    }
    else if (item->tme_display_menu_item_shortcut != NULL) {
      length = _tme_sdl_text_length(item->tme_display_menu_item_shortcut);
      _tme_sdl_text_draw(screen, rect.x + rect.w - border - (length + 1) * fw, y + pad / 2,
			 item->tme_display_menu_item_shortcut, text, length);
    }
    y += height;
  }
}

/* this draws the menu bar, the status line, and any open menus, over
   whatever has been drawn already: */
void
_tme_sdl_menu_draw(struct tme_sdl_screen *screen)
{
  struct tme_display_menus *menus;
  const struct tme_display_menu_item *item;
  const char *left, *right;
  Uint32 text;
  int output_width, output_height, bar, fw, pad, border, index, x, width;
  int columns, left_length, right_length;

  menus = screen->screen.tme_screen_menus;
  if (menus == NULL) {
    return;
  }
  _tme_sdl_menu_check(screen);
  _tme_sdl_output_size(screen, &output_width, &output_height);
  bar = screen->tme_sdl_screen_bar_height;
  fw = screen->tme_sdl_screen_font_width;
  pad = screen->tme_sdl_screen_pad;
  border = _tme_sdl_menu_border(screen);

  /* the menu bar: */
  _tme_sdl_fill(screen, 0, 0, output_width, bar, TME_SDL_COLOR_BAR);
  _tme_sdl_fill(screen, 0, bar - border, output_width, border, TME_SDL_COLOR_SEPARATOR);
  for (index = 0, item = menus->tme_display_menus_bar->tme_display_menu_items;
       item != NULL;
       index++, item = item->tme_display_menu_item_next) {
    _tme_sdl_menu_bar_item_x(screen, index, &x, &width);
    text = TME_SDL_COLOR_TEXT;
    if (screen->tme_sdl_screen_menu_depth > 0
	&& screen->tme_sdl_screen_menu_opener[0] == index) {
      _tme_sdl_fill(screen, x, 0, width, bar - border, TME_SDL_COLOR_HIGHLIGHT);
      text = TME_SDL_COLOR_HIGHLIGHT_TEXT;
    }
    _tme_sdl_text_draw(screen, x + fw, pad, item->tme_display_menu_item_label, text, 64);
  }

  /* the status line has a message, or the mouse hint, at the left,
     and the media at the right.  if they don't both fit, the media
     get at least half of the line: */
  _tme_sdl_fill(screen, 0, output_height - bar, output_width, bar, TME_SDL_COLOR_BAR);
  _tme_sdl_fill(screen, 0, output_height - bar, output_width, border, TME_SDL_COLOR_SEPARATOR);
  left = _tme_display_status_get(&screen->screen, TME_DISPLAY_STATUS_MESSAGE);
  if (left == NULL) {
    left = _tme_display_status_get(&screen->screen, TME_DISPLAY_STATUS_MOUSE);
  }
  right = _tme_display_status_get(&screen->screen, TME_DISPLAY_STATUS_MEDIA);
  columns = (output_width / fw) - 2;
  left_length = _tme_sdl_text_length(left);
  right_length = _tme_sdl_text_length(right);
  if (left_length + 4 + right_length > columns) {
    right_length = TME_MIN(right_length, TME_MAX(columns / 2, columns - left_length - 4));
    left_length = TME_MAX(columns - right_length - 4, 0);
  }
  _tme_sdl_text_draw(screen, fw, output_height - bar + pad,
		     left, TME_SDL_COLOR_TEXT, left_length);
  _tme_sdl_text_draw(screen, output_width - (right_length + 1) * fw, output_height - bar + pad,
		     right, TME_SDL_COLOR_TEXT, right_length);

  /* the open menus: */
  for (index = 0; index < screen->tme_sdl_screen_menu_depth; index++) {
    _tme_sdl_menu_draw_level(screen, index);
  }

  screen->tme_sdl_screen_menus_drawn = menus->tme_display_menus_generation;
  screen->tme_sdl_screen_status_drawn = menus->tme_display_menus_status_generation;
}

/* this returns the index of the first or last item in a menu that can
   be activated from the keyboard, or -1: */
static int
_tme_sdl_menu_selectable(struct tme_sdl_screen *screen, int level,
			 int from, int step)
{
  const struct tme_display_menu *menu;
  const struct tme_display_menu_item *item;
  int count, index, tries;

  menu = _tme_sdl_menu_level(screen, level);
  for (count = 0, item = menu->tme_display_menu_items;
       item != NULL;
       count++, item = item->tme_display_menu_item_next);
  if (count == 0) {
    return (-1);
  }
  index = from;
  for (tries = 0; tries < count; tries++) {
    index = (index + step + count) % count;
    item = _tme_sdl_menu_nth(menu, index);
    if (item->tme_display_menu_item_enabled
	&& item->tme_display_menu_item_type != TME_DISPLAY_MENU_ITEM_SEPARATOR) {
      return (index);
    }
  }
  return (-1);
}

/* this opens a menu from the menu bar: */
static void
_tme_sdl_menu_open_bar(struct tme_sdl_screen *screen, int index, int keyboard)
{
  struct tme_sdl_display *display;

  /* the keys held down and the mouse go back to the host while the
     menus are open: */
  display = _tme_sdl_screen_display(screen);
  if (screen->tme_sdl_screen_menu_depth == 0) {
    _tme_sdl_mouse_mode_off(display);
    _tme_sdl_keys_release(display);
  }
  screen->tme_sdl_screen_menu_depth = 1;
  screen->tme_sdl_screen_menu_opener[0] = index;
  screen->tme_sdl_screen_menu_highlight[0]
    = (keyboard ? _tme_sdl_menu_selectable(screen, 0, -1, 1) : -1);
}

/* this opens the first menu, for the keyboard, returning nonzero if
   there are menus: */
int
_tme_sdl_menu_open(struct tme_sdl_screen *screen)
{
  if (_tme_sdl_menu_bar(screen) == NULL
      || _tme_sdl_menu_bar(screen)->tme_display_menu_items == NULL) {
    return (FALSE);
  }
  _tme_sdl_menu_open_bar(screen, 0, TRUE);
  _tme_sdl_screen_present(screen);
  return (TRUE);
}

/* this opens the submenu of the highlighted item at a level, if it
   has one: */
static int
_tme_sdl_menu_open_sub(struct tme_sdl_screen *screen, int level, int keyboard)
{
  const struct tme_display_menu_item *item;
  int index;

  index = screen->tme_sdl_screen_menu_highlight[level];
  item = _tme_sdl_menu_nth(_tme_sdl_menu_level(screen, level), index);
  if (item == NULL
      || !item->tme_display_menu_item_enabled
      || item->tme_display_menu_item_type != TME_DISPLAY_MENU_ITEM_SUBMENU
      || level + 1 >= TME_SDL_MENU_DEPTH) {
    return (FALSE);
  }
  screen->tme_sdl_screen_menu_depth = level + 2;
  screen->tme_sdl_screen_menu_opener[level + 1] = index;
  screen->tme_sdl_screen_menu_highlight[level + 1]
    = (keyboard ? _tme_sdl_menu_selectable(screen, level + 1, -1, 1) : -1);
  return (TRUE);
}

/* this closes the menus and activates the highlighted item at a
   level, if it can be: */
static void
_tme_sdl_menu_activate(struct tme_sdl_screen *screen, int level)
{
  struct tme_display_menu_item *item;

  item = _tme_sdl_menu_nth(_tme_sdl_menu_level(screen, level),
			   screen->tme_sdl_screen_menu_highlight[level]);
  if (item == NULL
      || !item->tme_display_menu_item_enabled
      || item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_SUBMENU
      || item->tme_display_menu_item_type == TME_DISPLAY_MENU_ITEM_SEPARATOR) {
    return;
  }
  screen->tme_sdl_screen_menu_depth = 0;
  _tme_sdl_screen_present(screen);
  _tme_display_menu_activate(&screen->screen, item);
}

/* this handles a mouse event when mouse mode is off.  it returns
   nonzero if the event was for the menus or the status line, and
   not for turning mouse mode on: */
int
_tme_sdl_menu_mouse_event(struct tme_sdl_screen *screen,
			  const SDL_Event *event)
{
  int x, y, window_width, window_height, output_width, output_height;
  int bar_index, level, index;

  if (_tme_sdl_menu_bar(screen) == NULL) {
    return (FALSE);
  }

  /* convert the window position to pixels: */
  if (event->type == SDL_MOUSEMOTION) {
    x = event->motion.x;
    y = event->motion.y;
  }
  else {
    x = event->button.x;
    y = event->button.y;
  }
  SDL_GetWindowSize(screen->tme_sdl_screen_window, &window_width, &window_height);
  _tme_sdl_output_size(screen, &output_width, &output_height);
  if (window_width > 0 && window_height > 0) {
    x = (x * output_width) / window_width;
    y = (y * output_height) / window_height;
  }
  bar_index = _tme_sdl_menu_bar_hit(screen, x, y);

  /* when no menu is open, a press on a menu bar item opens its menu,
     and other events in the menu bar or the status line are
     ignored: */
  _tme_sdl_menu_check(screen);
  if (screen->tme_sdl_screen_menu_depth == 0) {
    if (event->type == SDL_MOUSEBUTTONDOWN && bar_index >= 0) {
      _tme_sdl_menu_open_bar(screen, bar_index, FALSE);
      screen->tme_sdl_screen_menu_press_opened = TRUE;
      _tme_sdl_screen_present(screen);
      return (TRUE);
    }
    return (y < screen->tme_sdl_screen_bar_height
	    || y >= output_height - screen->tme_sdl_screen_bar_height);
  }

  level = _tme_sdl_menu_hit(screen, x, y, &index);
  switch (event->type) {

  /* moving over another menu bar item opens its menu, and moving
     over an item highlights it, and opens its submenu: */
  case SDL_MOUSEMOTION:
    if (bar_index >= 0) {
      if (bar_index != screen->tme_sdl_screen_menu_opener[0]) {
	_tme_sdl_menu_open_bar(screen, bar_index, FALSE);
      }
    }
    else if (level >= 0) {
      screen->tme_sdl_screen_menu_depth = level + 1;
      screen->tme_sdl_screen_menu_highlight[level] = index;
      _tme_sdl_menu_open_sub(screen, level, FALSE);
    }
    break;

  /* a press on the open menu's menu bar item, or outside of the
     menus, closes them: */
  case SDL_MOUSEBUTTONDOWN:
    screen->tme_sdl_screen_menu_press_opened = FALSE;
    if (bar_index >= 0) {
      if (bar_index == screen->tme_sdl_screen_menu_opener[0]) {
	screen->tme_sdl_screen_menu_depth = 0;
      }
      else {
	_tme_sdl_menu_open_bar(screen, bar_index, FALSE);
	screen->tme_sdl_screen_menu_press_opened = TRUE;
      }
    }
    else if (level < 0) {
      screen->tme_sdl_screen_menu_depth = 0;
    }
    break;

  /* a release on an item activates it.  this works both for a click
     on the item, and for a drag to it from the menu bar: */
  case SDL_MOUSEBUTTONUP:
    screen->tme_sdl_screen_menu_press_opened = FALSE;
    if (level >= 0 && index >= 0) {
      screen->tme_sdl_screen_menu_highlight[level] = index;
      _tme_sdl_menu_activate(screen, level);
      return (TRUE);
    }
    break;

  default:
    break;
  }
  _tme_sdl_screen_present(screen);
  return (TRUE);
}

/* this handles a key event.  F10 opens the menus, and while they're
   open, they take all keys.  it returns nonzero if the event was for
   the menus: */
int
_tme_sdl_menu_key_event(struct tme_sdl_screen *screen,
			const SDL_Event *event)
{
  int level, count, highlight;

  _tme_sdl_menu_check(screen);
  if (screen->tme_sdl_screen_menu_depth == 0) {
    if (event->type == SDL_KEYDOWN
	&& event->key.keysym.sym == SDLK_F10) {
      if (!event->key.repeat) {
	return (_tme_sdl_menu_open(screen));
      }
      return (_tme_sdl_menu_bar(screen) != NULL);
    }
    return (FALSE);
  }
  if (event->type != SDL_KEYDOWN) {
    return (TRUE);
  }

  level = screen->tme_sdl_screen_menu_depth - 1;
  highlight = screen->tme_sdl_screen_menu_highlight[level];
  for (count = 0;
       _tme_sdl_menu_nth(_tme_sdl_menu_bar(screen), count) != NULL;
       count++);
  switch (event->key.keysym.sym) {

  case SDLK_F10:
    screen->tme_sdl_screen_menu_depth = 0;
    break;

  case SDLK_ESCAPE:
    screen->tme_sdl_screen_menu_depth = level;
    break;

  case SDLK_UP:
  case SDLK_DOWN:
    screen->tme_sdl_screen_menu_highlight[level]
      = _tme_sdl_menu_selectable(screen, level,
				 (highlight < 0 && event->key.keysym.sym == SDLK_UP
				  ? 0
				  : highlight),
				 (event->key.keysym.sym == SDLK_UP ? -1 : 1));
    break;

  case SDLK_RIGHT:
    if (!_tme_sdl_menu_open_sub(screen, level, TRUE)) {
      _tme_sdl_menu_open_bar(screen, (screen->tme_sdl_screen_menu_opener[0] + 1) % count, TRUE);
    }
    break;

  case SDLK_LEFT:
    if (level > 0) {
      screen->tme_sdl_screen_menu_depth = level;
    }
    else {
      _tme_sdl_menu_open_bar(screen, (screen->tme_sdl_screen_menu_opener[0] + count - 1) % count, TRUE);
    }
    break;

  case SDLK_RETURN:
  case SDLK_KP_ENTER:
  case SDLK_SPACE:
    if (!_tme_sdl_menu_open_sub(screen, level, TRUE)) {
      _tme_sdl_menu_activate(screen, level);
      return (TRUE);
    }
    break;

  default:
    break;
  }
  _tme_sdl_screen_present(screen);
  return (TRUE);
}
