/* tmesh/tmesh-window.c - the window for choosing, making and editing machines: */

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

/* tmesh without a script opens this window, which has:

	the list of machines (the scripts used recently), with New,
	    Open, Edit, Duplicate, Forget, Settings and Start;
	the editor, with a page for each part of a machine;
	the settings: the folders that ROM images are looked for in.

   a machine is its tmesh script (see tmesh-config.c), in a folder of
   its own with its files.  a new machine starts as one of the kinds
   that tmesh-config.c knows.  when a machine is saved, the disk
   images that don't exist yet are created, ROM images chosen
   elsewhere are copied into its folder, ROM images it names that
   aren't there are copied from the ROM folders, and the keyboard
   macros are written if they're missing.

   the widgets are microui's (microui.c), drawn with SDL and the SDL
   display's font.  coordinates are in output pixels (twice the
   window's points on a HiDPI screen): */

#include <tme/common.h>

/* includes: */
#include "tmesh-config.h"
#include "tmesh-window.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "microui.h"
#include "host/display/sdl/sdl-font.h"

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
#endif /* __APPLE__ */

/* the window's size, in points: */
#define TMESH_WINDOW_W		(940)
#define TMESH_WINDOW_H		(660)

/* the edited text fields: */
#define TMESH_WINDOW_EDIT_LEN	(1024)
#define TMESH_WINDOW_EDITS	(400)

/* the most drives: */
#define TMESH_WINDOW_DRIVES	(32)

/* the screens and the editor's pages: */
#define SCREEN_LIST		(0)
#define SCREEN_EDITOR		(1)
#define SCREEN_SETTINGS		(2)

#define PAGE_GENERAL		(0)
#define PAGE_FILES		(1)
#define PAGE_DRIVES		(2)
#define PAGE_DISPLAY		(3)
#define PAGE_NETWORK		(4)
#define PAGE_SCRIPT		(5)
#define N_PAGES			(6)

static const char *_tmesh_window_pages[N_PAGES] = {
  "General", "Files", "Drives", "Display", "Network", "Script"
};

static const char *_tmesh_window_drive_kinds[] = { "Disk", "CD-ROM", "Tape" };

static const char *_tmesh_window_ids[] = { "0", "1", "2", "3", "4", "5", "6" };

static const char *_tmesh_window_idle_types[] = {
  "none", "netbsd32-type-0", "netbsd32-type-1", "sunos32-type-0"
};
static const char *_tmesh_window_idle_labels[] = {
  "None", "NetBSD (before 1.6)", "NetBSD", "SunOS 4"
};

static const char *_tmesh_window_nets[] = {
  "Not connected", "Over UDP, to another emulator", "A host interface (BPF)", "A tap interface"
};

/* the text of an edited text field: */
struct _tmesh_window_edit {
  const void *key;
  char buf[TMESH_WINDOW_EDIT_LEN];
};

/* the keys of text fields, which tell them apart: */
static char _tmesh_window_key_name;
static char _tmesh_window_key_ram[16];
static char _tmesh_window_key_file[64];
static char _tmesh_window_key_drive[TMESH_WINDOW_DRIVES];

/* the window: */
static struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *font;
  int scale;
  int font_w, font_h;
  int out_w, out_h;
  mu_Context *ctx;

  int screen;
  int quit;
  char *result;
  char *message;

  /* the list of machines: */
  int n_recent;
  char **recent;
  struct tmesh_config **recent_config;	/* NULL if unreadable */
  char **recent_err;
  int selected;

  /* the editor: */
  struct tmesh_config *config;
  int is_new;
  int fresh;				/* new, and gets a kind's script */
  int start_after_save;
  int page;
  int disk_mb[TMESH_WINDOW_DRIVES];	/* for disk images that don't exist */
  struct _tmesh_window_edit edits[TMESH_WINDOW_EDITS];
  int n_edits;

  /* editing the script of a running machine: */
  int running;

  /* text typed or pasted, given to microui a little at a time: */
  char pending_text[4096];
  size_t pending_len;

  /* the open drop-down list, if any: */
  mu_Container *open_list;

  /* the settings: */
  int n_rom_dirs;
  char **rom_dirs;

  /* the open file chooser, if any: */
  pid_t chooser_pid;
  int chooser_fd;
  char *chooser_output;
  size_t chooser_len;
  int chooser_folder;
  void (*chooser_done) _TME_P((const char *, int));
  int chooser_data;
} _tmesh_window;

#define ui _tmesh_window

/* the width of some characters, in output pixels: */
#define CH(n)		((n) * ui.font_w)

/* this sets the message shown at the bottom: */
static void
_tmesh_window_message(const char *format, ...)
{
  char buffer[1500];
  va_list ap;

  va_start(ap, format);
  vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);
  if (ui.message != NULL) {
    tme_free(ui.message);
  }
  ui.message = tme_strdup(buffer);
}

static void
_tmesh_window_message_clear(void)
{
  if (ui.message != NULL) {
    tme_free(ui.message);
  }
  ui.message = NULL;
}

/* this returns the next character of UTF-8 text, as Latin-1: */
static int
_tmesh_window_text_next(const char **_text, const char *end)
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
    if ((const char *) s + i >= end || (s[i] & 0xc0) != 0x80) {
      *_text += 1;
      return (s[0]);
    }
    c = (c << 6) | (s[i] & 0x3f);
  }
  *_text += 1 + more;
  return (c <= 0xff ? (int) c : '?');
}

static int
_tmesh_window_text_width(mu_Font font, const char *text, int length)
{
  const char *end;
  int count;

  end = text + (length < 0 ? (int) strlen(text) : length);
  count = 0;
  while (text < end && *text != '\0') {
    _tmesh_window_text_next(&text, end);
    count++;
  }
  return (count * ui.font_w);
}

static int
_tmesh_window_text_height(mu_Font font)
{
  return (ui.font_h);
}

/* this makes the font texture for the window's scale: */
static void
_tmesh_window_make_font(void)
{
  int window_w, window_h, width, height, zoom, bytes, c, row, col, scale, i;
  const unsigned char *glyph;
  SDL_Surface *surface;
  Uint32 *pixels;
  mu_Style *style;

  SDL_GetWindowSize(ui.window, &window_w, &window_h);
  if (SDL_GetRendererOutputSize(ui.renderer, &ui.out_w, &ui.out_h) != 0) {
    ui.out_w = window_w;
    ui.out_h = window_h;
  }
  scale = (window_w > 0 ? (ui.out_w + window_w / 2) / window_w : 1);
  if (scale < 1) {
    scale = 1;
  }
  if (ui.font != NULL && scale == ui.scale) {
    return;
  }
  ui.scale = scale;

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

  if (ui.font != NULL) {
    SDL_DestroyTexture(ui.font);
  }
  ui.font = NULL;
  surface = SDL_CreateRGBSurfaceWithFormat(0, 16 * width, 14 * height, 32,
					   SDL_PIXELFORMAT_ARGB8888);
  if (surface != NULL) {
    SDL_LockSurface(surface);
    for (c = TME_SDL_FONT_FIRST; c <= TME_SDL_FONT_LAST; c++) {
      i = c - TME_SDL_FONT_FIRST;
      glyph = (width == TME_SDL_FONT_16X32_WIDTH
	       ? _tme_sdl_font_16x32[i]
	       : _tme_sdl_font_8x16[i]);
      for (row = 0; row < height; row++) {
	pixels = ((Uint32 *) ((Uint8 *) surface->pixels
			      + ((i / 16) * height + row) * surface->pitch)
		  + (i % 16) * width);
	for (col = 0; col < width; col++) {
	  pixels[col] = ((glyph[row * bytes + col / 8] & (0x80 >> (col % 8)))
			 ? 0xffffffff
			 : 0x00ffffff);
	}
      }
    }
    SDL_UnlockSurface(surface);
    ui.font = SDL_CreateTextureFromSurface(ui.renderer, surface);
    SDL_FreeSurface(surface);
  }
  if (ui.font != NULL) {
    SDL_SetTextureBlendMode(ui.font, SDL_BLENDMODE_BLEND);
  }

  ui.font_w = width * zoom;
  ui.font_h = height * zoom;

  /* the sizes of things, in output pixels: */
  style = ui.ctx->style;
  style->size = mu_vec2(10 * ui.font_w, ui.font_h + 2 * scale);
  style->padding = 4 * scale;
  style->spacing = 4 * scale;
  style->indent = 2 * ui.font_w;
  style->title_height = ui.font_h + 8 * scale;
  style->scrollbar_size = 12 * scale;
  style->thumb_size = 8 * scale;
}

static void
_tmesh_window_draw_text(const char *text, int x, int y, mu_Color color)
{
  const char *end;
  int width, height, c;
  SDL_Rect src, dst;

  if (ui.font == NULL) {
    return;
  }
  width = (ui.scale >= 2 ? TME_SDL_FONT_16X32_WIDTH : TME_SDL_FONT_8X16_WIDTH);
  height = (ui.scale >= 2 ? TME_SDL_FONT_16X32_HEIGHT : TME_SDL_FONT_8X16_HEIGHT);
  SDL_SetTextureColorMod(ui.font, color.r, color.g, color.b);
  end = text + strlen(text);
  while (text < end) {
    c = _tmesh_window_text_next(&text, end);
    if (c < TME_SDL_FONT_FIRST || c > TME_SDL_FONT_LAST) {
      c = '?';
    }
    c -= TME_SDL_FONT_FIRST;
    src.x = (c % 16) * width;
    src.y = (c / 16) * height;
    src.w = width;
    src.h = height;
    dst.x = x;
    dst.y = y;
    dst.w = ui.font_w;
    dst.h = ui.font_h;
    SDL_RenderCopy(ui.renderer, ui.font, &src, &dst);
    x += ui.font_w;
  }
}

static void
_tmesh_window_draw_rect(mu_Rect r, mu_Color color)
{
  SDL_Rect rect;

  rect.x = r.x;
  rect.y = r.y;
  rect.w = r.w;
  rect.h = r.h;
  SDL_SetRenderDrawColor(ui.renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(ui.renderer, &rect);
}

static void
_tmesh_window_draw_icon(int id, mu_Rect r, mu_Color color)
{
  const char *c;
  int i, w, x, y, m;

  /* the arrow of a drop-down list: */
  if (id == MU_ICON_MAX) {
    w = r.w / 2;
    x = r.x + (r.w - w) / 2;
    y = r.y + (r.h - w / 2) / 2;
    for (i = 0; i < w / 2; i++) {
      _tmesh_window_draw_rect(mu_rect(x + i, y + i, w - 2 * i, 1), color);
    }
    return;
  }
  if (id == MU_ICON_CHECK) {
    m = r.w / 4;
    _tmesh_window_draw_rect(mu_rect(r.x + m, r.y + m, r.w - 2 * m, r.h - 2 * m), color);
    return;
  }
  c = (id == MU_ICON_CLOSE ? "x"
       : id == MU_ICON_COLLAPSED ? "+"
       : id == MU_ICON_EXPANDED ? "-"
       : NULL);
  if (c != NULL) {
    _tmesh_window_draw_text(c, r.x + (r.w - ui.font_w) / 2, r.y + (r.h - ui.font_h) / 2, color);
  }
}

static void
_tmesh_window_render(void)
{
  mu_Command *cmd;
  SDL_Rect rect;

  SDL_RenderSetClipRect(ui.renderer, NULL);
  SDL_SetRenderDrawColor(ui.renderer, 0xf0, 0xf0, 0xf0, 255);
  SDL_RenderClear(ui.renderer);
  cmd = NULL;
  while (mu_next_command(ui.ctx, &cmd)) {
    switch (cmd->type) {
    case MU_COMMAND_TEXT:
      _tmesh_window_draw_text(cmd->text.str, cmd->text.pos.x, cmd->text.pos.y, cmd->text.color);
      break;
    case MU_COMMAND_RECT:
      _tmesh_window_draw_rect(cmd->rect.rect, cmd->rect.color);
      break;
    case MU_COMMAND_ICON:
      _tmesh_window_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color);
      break;
    case MU_COMMAND_CLIP:
      rect.x = cmd->clip.rect.x;
      rect.y = cmd->clip.rect.y;
      rect.w = cmd->clip.rect.w;
      rect.h = cmd->clip.rect.h;
      SDL_RenderSetClipRect(ui.renderer, (rect.w >= 0x1000000 ? NULL : &rect));
      break;
    }
  }
  SDL_RenderPresent(ui.renderer);
}

static void
_tmesh_window_set_style(void)
{
  static const struct { int id; unsigned int rgb; } colors[] = {
    { MU_COLOR_TEXT,		0x000000 },
    { MU_COLOR_BORDER,		0x9a9a9a },
    { MU_COLOR_WINDOWBG,	0xf0f0f0 },
    { MU_COLOR_TITLEBG,		0xe0e0e0 },
    { MU_COLOR_TITLETEXT,	0x000000 },
    { MU_COLOR_PANELBG,		0xfafafa },
    { MU_COLOR_BUTTON,		0xe2e2e2 },
    { MU_COLOR_BUTTONHOVER,	0xd2d2d2 },
    { MU_COLOR_BUTTONFOCUS,	0xc2c2c2 },
    { MU_COLOR_BASE,		0xffffff },
    { MU_COLOR_BASEHOVER,	0xf6f6f6 },
    { MU_COLOR_BASEFOCUS,	0xfffbe6 },
    { MU_COLOR_SCROLLBASE,	0xe6e6e6 },
    { MU_COLOR_SCROLLTHUMB,	0xa8a8a8 },
  };
  unsigned int i;

  for (i = 0; i < TME_ARRAY_ELS(colors); i++) {
    ui.ctx->style->colors[colors[i].id]
      = mu_color(colors[i].rgb >> 16, (colors[i].rgb >> 8) & 0xff, colors[i].rgb & 0xff, 255);
  }
}

/* the height of a row of widgets, with the space after it: */
static int
_tmesh_window_row_height(void)
{
  mu_Style *style;

  style = ui.ctx->style;
  return (style->size.y + 2 * style->padding + style->spacing);
}

static void
_tmesh_window_row(int n, const int *widths)
{
  mu_layout_row(ui.ctx, n, widths, 0);
}

static void
_tmesh_window_edits_clear(void)
{
  ui.n_edits = 0;
}

/* this returns the edited text of a field: */
static struct _tmesh_window_edit *
_tmesh_window_edit(const void *key, const char *value)
{
  struct _tmesh_window_edit *edit;
  int i;

  for (i = 0; i < ui.n_edits; i++) {
    if (ui.edits[i].key == key) {
      return (&ui.edits[i]);
    }
  }
  if (ui.n_edits >= TMESH_WINDOW_EDITS) {
    _tmesh_window_edits_clear();
  }
  edit = &ui.edits[ui.n_edits++];
  edit->key = key;
  snprintf(edit->buf, sizeof(edit->buf), "%s", (value != NULL ? value : ""));
  return (edit);
}

/* a text field.  the key tells fields apart, and the value is shown
   until the field is edited.  it returns the new text when it
   changes, or NULL: */
static const char *
_tmesh_window_text_field(const void *key, const char *value)
{
  struct _tmesh_window_edit *edit;

  edit = _tmesh_window_edit(key, value);
  if (mu_textbox_ex(ui.ctx, edit->buf, sizeof(edit->buf), 0) & MU_RES_CHANGE) {
    return (edit->buf);
  }
  return (NULL);
}

/* an item in a list, highlighted if selected.  it returns nonzero
   if clicked: */
static int
_tmesh_window_list_item(const void *id_data, int id_len, const char *label, int selected)
{
  mu_Context *ctx;
  mu_Id id;
  mu_Rect r;
  mu_Vec2 pos;
  int clicked;

  ctx = ui.ctx;
  id = mu_get_id(ctx, id_data, id_len);
  r = mu_layout_next(ctx);
  mu_update_control(ctx, id, r, 0);
  clicked = (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id);
  if (selected) {
    mu_draw_rect(ctx, r, mu_color(0x2f, 0x5f, 0xb3, 255));
    pos.x = r.x + ctx->style->padding;
    pos.y = r.y + (r.h - ui.font_h) / 2;
    mu_push_clip_rect(ctx, r);
    mu_draw_text(ctx, NULL, label, -1, pos, mu_color(255, 255, 255, 255));
    mu_pop_clip_rect(ctx);
  }
  else {
    if (ctx->hover == id) {
      mu_draw_rect(ctx, r, mu_color(0xe4, 0xec, 0xf8, 255));
    }
    mu_draw_control_text(ctx, label, r, MU_COLOR_TEXT, 0);
  }
  return (clicked);
}

/* a drop-down list: a button that shows the choice made, and opens
   the list of choices under it.  it returns nonzero if a choice was
   made.  if *sel isn't one of them, "Choose..." is shown: */
#define TMESH_WINDOW_DROPDOWN_ROWS	(14)
static int
_tmesh_window_dropdown(const char *name, const char **labels, int n, int *sel)
{
  mu_Context *ctx;
  mu_Container *cnt;
  mu_Rect r, tr;
  mu_Id id;
  int changed, i, step, h;
  int full[1] = { -1 };

  ctx = ui.ctx;
  changed = FALSE;
  step = _tmesh_window_row_height();
  mu_push_id(ctx, name, (int) strlen(name));
  id = mu_get_id(ctx, "!button", 7);
  r = mu_layout_next(ctx);
  mu_update_control(ctx, id, r, 0);
  cnt = mu_get_container(ctx, "!list");

  /* (a container is made open; only the list opened here is.) */
  if (cnt != ui.open_list) {
    cnt->open = 0;
  }

  if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id) {
    if (cnt->open && ui.open_list == cnt) {
      cnt->open = 0;
    }
    else if (n > 0) {
      h = ((n < TMESH_WINDOW_DROPDOWN_ROWS ? n : TMESH_WINDOW_DROPDOWN_ROWS) * step
	   + ctx->style->padding * 2 - ctx->style->spacing);
      mu_open_popup(ctx, "!list");
      cnt->rect = mu_rect(r.x, r.y + r.h, r.w, h);
      if (cnt->rect.y + h > ui.out_h && r.y - h >= 0) {
	cnt->rect.y = r.y - h;
      }
      cnt->scroll = mu_vec2(0, (*sel > 2 && *sel < n ? (*sel - 2) * step : 0));
      ui.open_list = cnt;
    }
  }

  mu_draw_control_frame(ctx, id, r, MU_COLOR_BUTTON, 0);
  tr = r;
  tr.w -= r.h;
  mu_push_clip_rect(ctx, tr);
  mu_draw_control_text(ctx, (*sel >= 0 && *sel < n ? labels[*sel] : "Choose..."),
		       tr, MU_COLOR_TEXT, 0);
  mu_pop_clip_rect(ctx);
  mu_draw_icon(ctx, MU_ICON_MAX, mu_rect(r.x + r.w - r.h, r.y, r.h, r.h),
	       ctx->style->colors[MU_COLOR_TEXT]);

  if (mu_begin_window_ex(ctx, "!list", mu_rect(0, 0, 0, 0),
			 MU_OPT_POPUP | MU_OPT_NORESIZE | MU_OPT_NOTITLE | MU_OPT_CLOSED)) {
    _tmesh_window_row(1, full);
    for (i = 0; i < n; i++) {
      if (_tmesh_window_list_item(&labels[i], sizeof(labels[i]), labels[i], i == *sel)) {
	*sel = i;
	changed = TRUE;
	cnt->open = 0;
      }
    }
    mu_end_window(ctx);
  }
  mu_pop_id(ctx);
  return (changed);
}

/* this shows long text, like a path, keeping its end when it doesn't
   fit: */
static void
_tmesh_window_label_end(const char *text)
{
  mu_Rect r;
  int fit, length;

  r = mu_layout_next(ui.ctx);
  mu_layout_set_next(ui.ctx, r, 0);
  fit = (r.w - 2 * ui.ctx->style->padding) / (ui.font_w > 0 ? ui.font_w : 1);
  length = (int) strlen(text);
  if (fit > 3 && length > fit) {
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "...%s", text + length - (fit - 3));
    mu_label(ui.ctx, buffer);
  }
  else {
    mu_label(ui.ctx, text);
  }
}

/* file choosers.  SDL has none, so the desktop's runs, as another
   process: osascript on macOS, and zenity or kdialog elsewhere.  it
   prints the path chosen: */

#ifndef __APPLE__
/* this returns nonzero if a program is in the PATH: */
static int
_tmesh_window_in_path(const char *program)
{
  const char *path;
  char file[1024];
  size_t length;

  for (path = getenv("PATH"); path != NULL && *path != '\0';) {
    length = strcspn(path, ":");
    snprintf(file, sizeof(file), "%.*s/%s", (int) length, path, program);
    if (length > 0 && access(file, X_OK) == 0) {
      return (TRUE);
    }
    path += length;
    if (*path == ':') {
      path++;
    }
  }
  return (FALSE);
}
#endif /* !__APPLE__ */

/* this appends an AppleScript string to a buffer: */
static void
_tmesh_window_applescript(char *buffer, size_t size, const char *text)
{
  size_t length;

  length = strlen(buffer);
  if (length + 1 < size) {
    buffer[length++] = '"';
  }
  for (; *text != '\0' && length + 3 < size; text++) {
    if (*text == '"' || *text == '\\') {
      buffer[length++] = '\\';
    }
    buffer[length++] = *text;
  }
  if (length + 1 < size) {
    buffer[length++] = '"';
  }
  buffer[length] = '\0';
}

/* this opens a file chooser: for a file to open, for the name of a
   new file (with the name given as the default), or for a folder.
   the done function is called with the path chosen, or NULL: */
static void
_tmesh_window_choose(const char *title, const char *start, const char *save_name,
		     int folder, void (*done) _TME_P((const char *, int)), int data)
{
  const char *argv[8];
  char script[3000], arg1[300], arg2[1200], *abs_start;
  struct stat st;
  int argc, fds[2], fd;

  if (ui.chooser_pid > 0) {
    _tmesh_window_message("A file chooser is already open");
    return;
  }
  if (start == NULL) {
    start = tmesh_config_last_dir();
  }
  if (start != NULL && (stat(start, &st) != 0 || !S_ISDIR(st.st_mode))) {
    start = NULL;
  }
  abs_start = (start != NULL ? realpath(start, NULL) : NULL);

  argc = 0;
#ifdef __APPLE__
  snprintf(script, sizeof(script), "activate\nset chosen to %s with prompt ",
	   (folder ? "choose folder" : save_name != NULL ? "choose file name" : "choose file"));
  _tmesh_window_applescript(script, sizeof(script), title);
  if (abs_start != NULL) {
    strcat(script, " default location (POSIX file ");
    _tmesh_window_applescript(script, sizeof(script), abs_start);
    strcat(script, ")");
  }
  if (save_name != NULL) {
    strcat(script, " default name ");
    _tmesh_window_applescript(script, sizeof(script), save_name);
  }
  strcat(script, "\nreturn POSIX path of chosen\n");
  argv[argc++] = "osascript";
  argv[argc++] = "-e";
  argv[argc++] = script;
  (void) arg1;
  (void) arg2;
#else  /* !__APPLE__ */
  (void) script;
  if (_tmesh_window_in_path("zenity")) {
    argv[argc++] = "zenity";
    argv[argc++] = "--file-selection";
    if (save_name != NULL) {
      argv[argc++] = "--save";
    }
    if (folder) {
      argv[argc++] = "--directory";
    }
    snprintf(arg1, sizeof(arg1), "--title=%s", title);
    argv[argc++] = arg1;
    if (abs_start != NULL || save_name != NULL) {
      snprintf(arg2, sizeof(arg2), "--filename=%s/%s",
	       (abs_start != NULL ? abs_start : "."),
	       (save_name != NULL ? save_name : ""));
      argv[argc++] = arg2;
    }
  }
  else if (_tmesh_window_in_path("kdialog")) {
    argv[argc++] = "kdialog";
    argv[argc++] = "--title";
    argv[argc++] = title;
    argv[argc++] = (folder ? "--getexistingdirectory"
		    : save_name != NULL ? "--getsavefilename"
		    : "--getopenfilename");
    snprintf(arg2, sizeof(arg2), "%s%s%s",
	     (abs_start != NULL ? abs_start : "."),
	     (save_name != NULL ? "/" : ""),
	     (save_name != NULL ? save_name : ""));
    argv[argc++] = arg2;
  }
  else {
    if (abs_start != NULL) {
      free(abs_start);
    }
    _tmesh_window_message("There is no file chooser: install zenity or kdialog, or type the path");
    return;
  }
#endif /* !__APPLE__ */
  argv[argc] = NULL;

  /* start the chooser, with its output to a pipe: */
  ui.chooser_pid = -1;
  if (pipe(fds) == 0) {
    ui.chooser_pid = fork();
    if (ui.chooser_pid == 0) {
      fd = open("/dev/null", O_RDWR);
      dup2(fd, STDIN_FILENO);
      dup2(fd, STDERR_FILENO);
      dup2(fds[1], STDOUT_FILENO);
      execvp(argv[0], (char * const *) argv);
      _exit(127);
    }
    close(fds[1]);
    if (ui.chooser_pid < 0) {
      close(fds[0]);
    }
  }
  if (abs_start != NULL) {
    free(abs_start);
  }
  if (ui.chooser_pid < 0) {
    ui.chooser_pid = 0;
    _tmesh_window_message("Could not open a file chooser: %s", strerror(errno));
    return;
  }
  ui.chooser_fd = fds[0];
  fcntl(ui.chooser_fd, F_SETFL, fcntl(ui.chooser_fd, F_GETFL) | O_NONBLOCK);
  fcntl(ui.chooser_fd, F_SETFD, FD_CLOEXEC);
  ui.chooser_len = 0;
  ui.chooser_folder = folder;
  ui.chooser_done = done;
  ui.chooser_data = data;
}

/* this checks on the open file chooser: */
static void
_tmesh_window_choose_poll(void)
{
  char buffer[1024], *path, *dir, *slash;
  ssize_t got;
  size_t length;
  int status;

  if (ui.chooser_pid <= 0) {
    return;
  }
  for (;;) {
    got = read(ui.chooser_fd, buffer, sizeof(buffer));
    if (got < 0 && errno == EINTR) {
      continue;
    }
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    if (got <= 0) {
      break;
    }
    ui.chooser_output = (ui.chooser_output == NULL
			 ? tme_new(char, ui.chooser_len + got + 1)
			 : tme_renew(char, ui.chooser_output, ui.chooser_len + got + 1));
    memcpy(ui.chooser_output + ui.chooser_len, buffer, got);
    ui.chooser_len += got;
  }
  close(ui.chooser_fd);
  while (waitpid(ui.chooser_pid, &status, 0) < 0 && errno == EINTR);
  ui.chooser_pid = 0;

  /* the path is the first line.  (osascript ends a folder with a
     slash.)  */
  path = NULL;
  if (ui.chooser_len > 0) {
    ui.chooser_output[ui.chooser_len] = '\0';
    ui.chooser_output[strcspn(ui.chooser_output, "\n")] = '\0';
    length = strlen(ui.chooser_output);
    while (length > 1 && ui.chooser_output[length - 1] == '/') {
      ui.chooser_output[--length] = '\0';
    }
    if (length > 0) {
      path = ui.chooser_output;
    }
  }

  /* remember the folder, for the next chooser: */
  if (path != NULL) {
    dir = tme_strdup(path);
    if (!ui.chooser_folder) {
      slash = strrchr(dir, '/');
      if (slash != NULL) {
	slash[slash == dir] = '\0';
      }
    }
    tmesh_config_set_last_dir(dir);
    tme_free(dir);
  }
  SDL_RaiseWindow(ui.window);
  (*ui.chooser_done)(path, ui.chooser_data);
  if (ui.chooser_output != NULL) {
    tme_free(ui.chooser_output);
    ui.chooser_output = NULL;
  }
}

/* the list of machines: */

static void
_tmesh_window_list_free(void)
{
  int i;

  for (i = 0; i < ui.n_recent; i++) {
    tmesh_config_free(ui.recent_config[i]);
    if (ui.recent_err[i] != NULL) {
      tme_free(ui.recent_err[i]);
    }
  }
  tmesh_config_list_free(ui.n_recent, ui.recent);
  if (ui.recent_config != NULL) {
    tme_free(ui.recent_config);
    tme_free(ui.recent_err);
  }
  ui.recent = NULL;
  ui.recent_config = NULL;
  ui.recent_err = NULL;
  ui.n_recent = 0;
}

static void
_tmesh_window_list_load(const char *select)
{
  int i;

  _tmesh_window_list_free();
  ui.n_recent = tmesh_config_recent(&ui.recent);
  ui.recent_config = tme_new0(struct tmesh_config *, ui.n_recent + 1);
  ui.recent_err = tme_new0(char *, ui.n_recent + 1);
  for (i = 0; i < ui.n_recent; i++) {
    ui.recent_config[i] = tmesh_config_load(ui.recent[i], &ui.recent_err[i]);
  }
  if (ui.selected >= ui.n_recent) {
    ui.selected = ui.n_recent - 1;
  }
  if (ui.selected < 0) {
    ui.selected = 0;
  }
  for (i = 0; select != NULL && i < ui.n_recent; i++) {
    if (strcmp(ui.recent[i], select) == 0) {
      ui.selected = i;
    }
  }
}

/* this opens the editor: */
static void
_tmesh_window_open_editor(struct tmesh_config *config, int is_new)
{
  const struct tmesh_config_kind *kind;
  int i;

  tmesh_config_free(ui.config);
  ui.config = config;
  ui.is_new = is_new;
  ui.fresh = FALSE;
  ui.open_list = NULL;
  ui.page = PAGE_GENERAL;
  ui.start_after_save = FALSE;
  kind = tmesh_config_kind(config);
  for (i = 0; i < TMESH_WINDOW_DRIVES; i++) {
    ui.disk_mb[i] = (kind != NULL ? kind->tmesh_config_kind_disk_mb : 1024);
  }
  _tmesh_window_edits_clear();
  _tmesh_window_message_clear();
  ui.screen = SCREEN_EDITOR;
}

/* this makes a new machine of a kind, keeping the name if the user
   typed one: */
static void
_tmesh_window_new_kind(int kind_i, const char *name)
{
  const struct tmesh_config_kind *kinds;
  struct tmesh_config *config;
  int count;

  kinds = tmesh_config_kinds(&count);
  config = tmesh_config_parse(kinds[kind_i].tmesh_config_kind_script, NULL);
  if (name != NULL) {
    tmesh_config_set_name(config, name);
  }
  _tmesh_window_open_editor(config, TRUE);
  ui.fresh = TRUE;
}

/* a machine was chosen to add to the list: */
static void
_tmesh_window_opened(const char *path, int data)
{
  struct tmesh_config *config;
  char *err;

  if (path == NULL) {
    return;
  }
  config = tmesh_config_load(path, &err);
  if (config == NULL) {
    _tmesh_window_message("%s", err);
    tme_free(err);
    return;
  }
  tmesh_config_add_recent(config->tmesh_config_path);
  _tmesh_window_list_load(config->tmesh_config_path);
  tmesh_config_free(config);
  _tmesh_window_message("Added %s", path);
}

static int _tmesh_window_files _TME_P((struct tmesh_config *));

/* this starts a machine, by returning its script from the window.
   first, its missing files are made or copied, as when it's saved: */
static void
_tmesh_window_start(const struct tmesh_config *config)
{
  struct tmesh_config *copy;
  char *problem;

  if (config->tmesh_config_path == NULL) {
    return;
  }
  copy = tmesh_config_copy(config);
  if (!_tmesh_window_files(copy)) {
    tmesh_config_free(copy);
    return;
  }
  problem = tmesh_config_check(copy);
  tmesh_config_free(copy);
  if (problem != NULL) {
    _tmesh_window_message("Can't start: %s", problem);
    tme_free(problem);
    return;
  }
  tmesh_config_add_recent(config->tmesh_config_path);
  ui.result = tme_strdup(config->tmesh_config_path);
}

/* this makes a copy of a machine, to save somewhere else.  its ROM
   images and keyboard macros come along (they're copied when it's
   saved), its CD-ROM and tape images stay where they are, and its
   disk images and state files that are in its folder are made new: */
static struct tmesh_config *
_tmesh_window_duplicate(const struct tmesh_config *config)
{
  struct tmesh_config *copy;
  struct tmesh_config_file *files;
  struct tmesh_config_drive *drives;
  const char *file;
  char *path, name[300];
  int count, i;

  copy = tmesh_config_copy(config);
  count = tmesh_config_files(copy, &files);
  for (i = 0; i < count; i++) {
    if (files[i].tmesh_config_file_kind == TMESH_CONFIG_FILE_STATE) {
      continue;
    }
    file = tmesh_config_word(copy, files[i].tmesh_config_file_line, files[i].tmesh_config_file_arg);
    path = tmesh_config_resolve(copy, file);
    tmesh_config_set_word(copy, files[i].tmesh_config_file_line, files[i].tmesh_config_file_arg, path);
    tme_free(path);
  }
  if (files != NULL) {
    tme_free(files);
  }
  for (i = 0;; i++) {
    count = tmesh_config_drives(copy, &drives);
    if (i < count
	&& drives[i].tmesh_config_drive_kind != TMESH_CONFIG_DRIVE_DISK
	&& *drives[i].tmesh_config_drive_file != '\0'
	&& *drives[i].tmesh_config_drive_file != '/') {
      path = tmesh_config_resolve(copy, drives[i].tmesh_config_drive_file);
      tmesh_config_drive_set_file(copy, &drives[i], path);
      tme_free(path);
    }
    if (drives != NULL) {
      tme_free(drives);
    }
    if (i >= count) {
      break;
    }
  }
  snprintf(name, sizeof(name), "%s (copy)", tmesh_config_name(config));
  tmesh_config_set_name(copy, name);
  tme_free(copy->tmesh_config_path);
  tme_free(copy->tmesh_config_dir);
  copy->tmesh_config_path = NULL;
  copy->tmesh_config_dir = NULL;
  return (copy);
}

static void _tmesh_window_open_settings _TME_P((void));

static void
_tmesh_window_screen_list(void)
{
  mu_Context *ctx;
  struct tmesh_config *selected;
  char *summary;
  const char *name;
  int i;
  int cols[2] = { CH(40), -1 };
  int full[1] = { -1 };
  int bw[8] = { CH(10), CH(10), CH(8), CH(12), CH(9), CH(13), -CH(12), -1 };

  ctx = ui.ctx;
  selected = (ui.selected < ui.n_recent ? ui.recent_config[ui.selected] : NULL);
  mu_layout_row(ctx, 2, cols, -_tmesh_window_row_height() * 2 - ctx->style->padding);

  /* the machines: */
  mu_begin_panel(ctx, "machines");
  _tmesh_window_row(1, full);
  if (ui.n_recent == 0) {
    mu_label(ctx, "No machines yet.");
    mu_text(ctx, "Make one with New..., or add an existing tmesh script (a tme.cfg) with Open....");
  }
  for (i = 0; i < ui.n_recent; i++) {
    name = (ui.recent_config[i] != NULL ? tmesh_config_name(ui.recent_config[i]) : ui.recent[i]);
    if (_tmesh_window_list_item(&ui.recent[i], sizeof(ui.recent[i]), name, i == ui.selected)) {
      ui.selected = i;
    }
  }
  mu_end_panel(ctx);

  /* the selected one: */
  mu_begin_panel(ctx, "details");
  _tmesh_window_row(1, full);
  if (ui.selected < ui.n_recent) {
    if (selected != NULL) {
      summary = tmesh_config_summary(selected);
      mu_label(ctx, tmesh_config_name(selected));
      mu_text(ctx, summary);
      tme_free(summary);
    }
    else {
      mu_label(ctx, "Can't read this machine:");
      mu_text(ctx, ui.recent_err[ui.selected]);
    }
    mu_label(ctx, "");
    mu_text(ctx, ui.recent[ui.selected]);
  }
  mu_end_panel(ctx);

  /* the buttons: */
  _tmesh_window_row(8, bw);
  if (mu_button(ctx, "New...")) {
    _tmesh_window_new_kind(0, NULL);
  }
  if (mu_button(ctx, "Open...")) {
    _tmesh_window_choose("Open a machine (a tmesh script, like tme.cfg)",
			 (tmesh_config_last_dir() != NULL
			  ? tmesh_config_last_dir()
			  : tmesh_config_machines_dir()),
			 NULL, FALSE, _tmesh_window_opened, 0);
  }
  if (mu_button(ctx, "Edit") && selected != NULL) {
    _tmesh_window_open_editor(tmesh_config_copy(selected), FALSE);
  }
  if (mu_button(ctx, "Duplicate") && selected != NULL) {
    _tmesh_window_open_editor(_tmesh_window_duplicate(selected), TRUE);
  }
  if (mu_button(ctx, "Forget") && ui.selected < ui.n_recent) {
    tmesh_config_remove_recent(ui.recent[ui.selected]);
    _tmesh_window_list_load(NULL);
    _tmesh_window_message("Removed from the list (the files are still there)");
  }
  if (mu_button(ctx, "Settings...")) {
    _tmesh_window_open_settings();
  }
  mu_label(ctx, "");
  if (mu_button(ctx, "Start") && selected != NULL) {
    _tmesh_window_start(selected);
  }
}

/* the settings: */

static void
_tmesh_window_open_settings(void)
{
  tmesh_config_list_free(ui.n_rom_dirs, ui.rom_dirs);
  ui.n_rom_dirs = tmesh_config_rom_dirs(&ui.rom_dirs);
  _tmesh_window_edits_clear();
  _tmesh_window_message_clear();
  ui.screen = SCREEN_SETTINGS;
}

/* a ROM folder was chosen, for a ROM folder (data), or a new one
   (data -1): */
static void
_tmesh_window_rom_dir_chosen(const char *path, int data)
{
  if (path == NULL) {
    return;
  }
  if (data >= 0 && data < ui.n_rom_dirs) {
    tme_free(ui.rom_dirs[data]);
    ui.rom_dirs[data] = tme_strdup(path);
  }
  else {
    tmesh_config_list_add(&ui.n_rom_dirs, &ui.rom_dirs, path);
  }
  _tmesh_window_edits_clear();
  tmesh_config_set_rom_dirs(ui.n_rom_dirs, ui.rom_dirs);
}

static void
_tmesh_window_screen_settings(void)
{
  mu_Context *ctx;
  const char *text;
  int i, j;
  int full[1] = { -1 };
  int cw[4] = { CH(16), -CH(22), CH(11), CH(9) };
  int aw[2] = { CH(16), CH(14) };
  int bw[1] = { CH(10) };

  ctx = ui.ctx;
  mu_layout_row(ctx, 1, full, -_tmesh_window_row_height() * 2 - ctx->style->padding);
  mu_begin_panel(ctx, "settings");
  _tmesh_window_row(1, full);
  mu_label(ctx, "ROM images");
  mu_text(ctx,
	  "When a machine is saved, the ROM images that it names but doesn't "
	  "have in its folder are looked for by name in these folders, and "
	  "copied into its folder.  Put the PROM and FCode ROM images that "
	  "you have in one of them, and new machines get them by themselves.");
  mu_label(ctx, "");
  for (i = 0; i < ui.n_rom_dirs; i++) {
    mu_push_id(ctx, &i, sizeof(i));
    _tmesh_window_row(4, cw);
    mu_label(ctx, (i == 0 ? "ROM folders:" : ""));
    text = _tmesh_window_text_field(&ui.rom_dirs[i], ui.rom_dirs[i]);
    if (text != NULL) {
      tme_free(ui.rom_dirs[i]);
      ui.rom_dirs[i] = tme_strdup(text);
      tmesh_config_set_rom_dirs(ui.n_rom_dirs, ui.rom_dirs);
    }
    if (mu_button(ctx, "Browse...")) {
      _tmesh_window_choose("Choose a folder with ROM images", NULL, NULL, TRUE,
			   _tmesh_window_rom_dir_chosen, i);
    }
    if (mu_button(ctx, "Remove")) {
      tme_free(ui.rom_dirs[i]);
      for (j = i; j < ui.n_rom_dirs; j++) {
	ui.rom_dirs[j] = ui.rom_dirs[j + 1];
      }
      ui.n_rom_dirs--;
      _tmesh_window_edits_clear();
      tmesh_config_set_rom_dirs(ui.n_rom_dirs, ui.rom_dirs);
      mu_pop_id(ctx);
      break;
    }
    mu_pop_id(ctx);
  }
  _tmesh_window_row(2, aw);
  mu_label(ctx, (ui.n_rom_dirs == 0 ? "ROM folders:" : ""));
  if (mu_button(ctx, "Add folder...")) {
    _tmesh_window_choose("Choose a folder with ROM images", NULL, NULL, TRUE,
			 _tmesh_window_rom_dir_chosen, -1);
  }
  mu_end_panel(ctx);

  _tmesh_window_row(1, bw);
  if (mu_button(ctx, "Back")) {
    ui.screen = SCREEN_LIST;
  }
}

/* saving: */

/* this returns nonzero if a drive is a disk whose image doesn't
   exist yet: */
static int
_tmesh_window_disk_missing(const struct tmesh_config *config, const struct tmesh_config_drive *drive)
{
  struct stat st;
  char *path;
  int missing;

  if (drive->tmesh_config_drive_kind != TMESH_CONFIG_DRIVE_DISK
      || *drive->tmesh_config_drive_file == '\0') {
    return (FALSE);
  }

  /* (an unsaved machine's disk images go in the folder it's saved in.)  */
  if (drive->tmesh_config_drive_file[0] != '/' && config->tmesh_config_dir == NULL) {
    return (TRUE);
  }
  path = tmesh_config_resolve(config, drive->tmesh_config_drive_file);
  missing = (stat(path, &st) != 0);
  tme_free(path);
  return (missing);
}

/* this returns nonzero if two paths are the same file: */
static int
_tmesh_window_same_file(const char *path0, const char *path1)
{
  struct stat st0, st1;

  return (stat(path0, &st0) == 0
	  && stat(path1, &st1) == 0
	  && st0.st_dev == st1.st_dev
	  && st0.st_ino == st1.st_ino);
}

/* this brings a saved machine's files into its folder: it creates its
   missing disk images and keyboard macros, and copies ROM images that
   were chosen elsewhere, or that it names and are in a ROM folder.
   it returns FALSE on an error: */
static int
_tmesh_window_files(struct tmesh_config *config)
{
  const struct tmesh_config_kind *kind;
  struct tmesh_config_drive *drives;
  struct tmesh_config_file *files;
  const char *file, *base;
  char *path, *from, *err;
  struct stat st;
  long long mb;
  int count, i, ok, changed;

  ok = TRUE;
  changed = FALSE;

  count = tmesh_config_files(config, &files);
  for (i = 0; i < count && ok; i++) {
    file = tmesh_config_word(config, files[i].tmesh_config_file_line, files[i].tmesh_config_file_arg);
    path = tmesh_config_resolve(config, file);
    base = strrchr(file, '/');
    base = (base != NULL ? base + 1 : file);

    /* the keyboard macros: */
    if (files[i].tmesh_config_file_kind == TMESH_CONFIG_FILE_MACROS
	&& stat(path, &st) != 0
	&& strchr(file, '/') == NULL) {
      ok = tmesh_config_write_macros(path, &err);
    }

    /* a ROM image.  one chosen elsewhere replaces any of the same
       name in the folder: */
    else if (files[i].tmesh_config_file_kind == TMESH_CONFIG_FILE_ROM) {
      from = NULL;
      if (file[0] == '/' && stat(path, &st) == 0) {
	from = tme_strdup(path);
      }
      else if (strchr(file, '/') == NULL && stat(path, &st) != 0) {
	from = tmesh_config_find_rom(file);
      }
      if (from != NULL) {
	tme_free(path);
	path = tmesh_config_resolve(config, base);
	ok = (_tmesh_window_same_file(from, path)
	      || tmesh_config_copy_file(from, path, &err));
	if (ok) {
	  tmesh_config_set_word(config, files[i].tmesh_config_file_line,
				files[i].tmesh_config_file_arg, base);
	  changed = TRUE;
	}
	tme_free(from);
      }
    }
    tme_free(path);
  }
  if (files != NULL) {
    tme_free(files);
  }

  /* the disk images, of the size given in the editor, or else the
     kind's size: */
  kind = tmesh_config_kind(config);
  count = tmesh_config_drives(config, &drives);
  for (i = 0; i < count && ok; i++) {
    if (_tmesh_window_disk_missing(config, &drives[i])) {
      mb = (config == ui.config && i < TMESH_WINDOW_DRIVES ? ui.disk_mb[i]
	    : kind != NULL ? kind->tmesh_config_kind_disk_mb
	    : 1024);
      path = tmesh_config_resolve(config, drives[i].tmesh_config_drive_file);
      ok = tmesh_config_create_disk(path, mb * 1024 * 1024, &err);
      tme_free(path);
    }
  }
  if (drives != NULL) {
    tme_free(drives);
  }

  if (!ok) {
    _tmesh_window_message("%s", err);
    tme_free(err);
    return (FALSE);
  }
  if (changed && !tmesh_config_save(config, config->tmesh_config_path, &err)) {
    _tmesh_window_message("%s", err);
    tme_free(err);
    return (FALSE);
  }
  return (TRUE);
}

/* this saves the machine in a file: */
static int
_tmesh_window_save_to(const char *path)
{
  char *err;

  if (!tmesh_config_save(ui.config, path, &err)) {
    _tmesh_window_message("%s", err);
    tme_free(err);
    return (FALSE);
  }
  _tmesh_window_edits_clear();
  if (!_tmesh_window_files(ui.config)) {
    return (FALSE);
  }
  tmesh_config_add_recent(ui.config->tmesh_config_path);
  _tmesh_window_list_load(ui.config->tmesh_config_path);
  ui.is_new = FALSE;
  ui.fresh = FALSE;
  _tmesh_window_message("Saved %s", ui.config->tmesh_config_path);
  return (TRUE);
}

/* a new machine's folder (or script) was chosen: */
static void
_tmesh_window_saved_as(const char *path, int data)
{
  struct stat st;
  char *file;
  size_t length;

  if (path == NULL) {
    return;
  }
  length = strlen(path);
  if (length > 4 && strcmp(path + length - 4, ".cfg") == 0) {
    file = tme_strdup(path);
  }
  else {
    if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) {
      _tmesh_window_message("%s is a file; choose a name for a folder for the machine", path);
      return;
    }
    if (!tmesh_config_make_dirs(path)) {
      _tmesh_window_message("Could not make the folder %s: %s", path, strerror(errno));
      return;
    }
    file = tme_new(char, length + 16);
    sprintf(file, "%s/tme.cfg", path);
    if (stat(file, &st) == 0) {
      _tmesh_window_message("%s already has a tme.cfg; choose another name", path);
      tme_free(file);
      return;
    }
  }
  if (_tmesh_window_save_to(file) && ui.start_after_save) {
    _tmesh_window_start(ui.config);
  }
  tme_free(file);
}

/* this saves the machine.  a new one is saved where the user chooses
   (later, so this returns FALSE), and then started if asked: */
static int
_tmesh_window_save(int then_start)
{
  char name[256];
  size_t i;

  if (ui.config->tmesh_config_path != NULL) {
    return (_tmesh_window_save_to(ui.config->tmesh_config_path));
  }
  snprintf(name, sizeof(name), "%s", tmesh_config_name(ui.config));
  for (i = 0; name[i] != '\0'; i++) {
    if (name[i] == '/' || name[i] == ':') {
      name[i] = '-';
    }
  }
  tmesh_config_make_dirs(tmesh_config_machines_dir());
  ui.start_after_save = then_start;
  _tmesh_window_choose("Save the machine as (a folder for it)",
		       (tmesh_config_last_dir() != NULL
			? tmesh_config_last_dir()
			: tmesh_config_machines_dir()),
		       name, FALSE, _tmesh_window_saved_as, 0);
  return (FALSE);
}

/* the editor's pages: */

static void
_tmesh_window_page_general(void)
{
  mu_Context *ctx;
  struct tmesh_config *config;
  const struct tmesh_config_kind *kinds, *kind;
  struct tmesh_config *default_config;
  const char *labels[16], *text;
  char name[300];
  int *ram, n_ram, n_kinds, kind_i, i, sel, mb, typed;
  int lw[2] = { CH(22), -1 };
  int rw[3] = { CH(22), CH(10), -1 };
  int full[1] = { -1 };

  ctx = ui.ctx;
  config = ui.config;
  kinds = tmesh_config_kinds(&n_kinds);
  kind = tmesh_config_kind(config);

  _tmesh_window_row(2, lw);
  mu_label(ctx, "Name");
  text = _tmesh_window_text_field(&_tmesh_window_key_name, tmesh_config_name(config));
  if (text != NULL) {
    tmesh_config_set_name(config, text);
  }

  /* a new machine can be any kind, and gets that kind's script: */
  mu_label(ctx, "Machine");
  if (ui.fresh) {
    kind_i = -1;
    for (i = 0; i < n_kinds && i < 16; i++) {
      labels[i] = kinds[i].tmesh_config_kind_label;
      if (kind == &kinds[i]) {
	kind_i = i;
      }
    }
    sel = kind_i;
    if (_tmesh_window_dropdown("kind", labels, (n_kinds < 16 ? n_kinds : 16), &sel)
	&& sel != kind_i) {

      /* keep a name that the user typed, instead of the kind's: */
      snprintf(name, sizeof(name), "%s", tmesh_config_name(config));
      default_config = (kind_i >= 0
			? tmesh_config_parse(kinds[kind_i].tmesh_config_kind_script, NULL)
			: NULL);
      typed = (default_config == NULL
	       || strcmp(name, tmesh_config_name(default_config)) != 0);
      tmesh_config_free(default_config);
      _tmesh_window_new_kind(sel, (typed ? name : NULL));
      return;
    }
  }
  else {
    mu_label(ctx, (kind != NULL ? kind->tmesh_config_kind_label : "(a tmesh script)"));
  }

  /* the memory: */
  n_ram = tmesh_config_ram(config, &ram);
  for (i = 0; i < n_ram; i++) {
    mu_push_id(ctx, &i, sizeof(i));
    _tmesh_window_row(3, rw);
    if (n_ram > 1) {
      snprintf(name, sizeof(name), "Memory (%s)", tmesh_config_word(config, ram[i], 0));
    }
    else {
      snprintf(name, sizeof(name), "Memory");
    }
    mu_label(ctx, name);
    mb = tmesh_config_ram_mb(config, ram[i]);
    snprintf(name, sizeof(name), "%d", mb);
    text = _tmesh_window_text_field(&_tmesh_window_key_ram[i % 16], name);
    if (text != NULL && atoi(text) > 0) {
      tmesh_config_set_ram_mb(config, ram[i], atoi(text));
    }
    mu_label(ctx, "MB");
    mu_pop_id(ctx);
  }
  tme_free(ram);

  /* the SPARC idle type: */
  if (kind != NULL && kind->tmesh_config_kind_sparc) {
    _tmesh_window_row(2, lw);
    mu_label(ctx, "Idle detection");
    sel = 0;
    for (i = 0; i < (int) TME_ARRAY_ELS(_tmesh_window_idle_types); i++) {
      if (strcmp(tmesh_config_idle_type(config), _tmesh_window_idle_types[i]) == 0) {
	sel = i;
      }
    }
    if (_tmesh_window_dropdown("idle", _tmesh_window_idle_labels,
			       TME_ARRAY_ELS(_tmesh_window_idle_labels), &sel)) {
      tmesh_config_set_idle_type(config, _tmesh_window_idle_types[sel]);
    }
    mu_label(ctx, "");
    mu_label(ctx, "(the guest's idle loop, so the emulator doesn't spin when it's idle)");
  }

  _tmesh_window_row(1, full);
  mu_label(ctx, "");
  if (kind != NULL) {
    mu_text(ctx, kind->tmesh_config_kind_description);
  }
  else {
    mu_text(ctx, "The window doesn't know this kind of machine, but can still change "
	    "its files, drives, display and network.  See the Script page for "
	    "everything else.");
  }
}

/* the file chooser's field: a word of a line of the script: */
static int _tmesh_window_pick_line, _tmesh_window_pick_arg;

/* a file was chosen for a word of the script: */
static void
_tmesh_window_word_chosen(const char *path, int data)
{
  char *name;

  if (path == NULL || _tmesh_window_pick_line >= ui.config->tmesh_config_n_lines) {
    return;
  }
  name = tmesh_config_shorten(ui.config, path);
  tmesh_config_set_word(ui.config, _tmesh_window_pick_line, _tmesh_window_pick_arg, name);
  tme_free(name);
  _tmesh_window_edits_clear();
}

/* this says whether a file is there: */
static void
_tmesh_window_file_status(const struct tmesh_config_file *file)
{
  const char *word;
  char *path, *found, text[1200];
  struct stat st;

  word = tmesh_config_word(ui.config, file->tmesh_config_file_line, file->tmesh_config_file_arg);
  path = tmesh_config_resolve(ui.config, word);
  if (stat(path, &st) == 0) {
    if (file->tmesh_config_file_kind == TMESH_CONFIG_FILE_ROM && word[0] == '/') {
      snprintf(text, sizeof(text), "(copied into the machine's folder when it's saved)");
    }
    else {
      snprintf(text, sizeof(text), "(%lld bytes)", (long long) st.st_size);
    }
  }
  else if (file->tmesh_config_file_kind == TMESH_CONFIG_FILE_STATE) {
    snprintf(text, sizeof(text), "(made when the machine first starts)");
  }
  else if (file->tmesh_config_file_kind == TMESH_CONFIG_FILE_MACROS) {
    snprintf(text, sizeof(text), "(written when the machine is saved)");
  }
  else if (file->tmesh_config_file_kind == TMESH_CONFIG_FILE_ROM
	   && strchr(word, '/') == NULL
	   && (found = tmesh_config_find_rom(word)) != NULL) {
    snprintf(text, sizeof(text), "(copied from %s when the machine is saved)", found);
    tme_free(found);
  }
  else {
    snprintf(text, sizeof(text), "(missing: choose it, or put it in a ROM folder under Settings)");
  }
  tme_free(path);
  _tmesh_window_label_end(text);
}

static void
_tmesh_window_page_files(void)
{
  mu_Context *ctx;
  struct tmesh_config *config;
  struct tmesh_config_file *files;
  const char *word, *text;
  char title[200];
  int count, i;
  int fw[3] = { CH(22), -CH(10), -1 };
  int sw[2] = { CH(22), -1 };
  int full[1] = { -1 };

  ctx = ui.ctx;
  config = ui.config;
  count = tmesh_config_files(config, &files);
  _tmesh_window_row(1, full);
  mu_text(ctx, "The machine's ROM images, and the files where it keeps its state.  "
	  "File names are relative to the machine's folder.");
  mu_label(ctx, "");
  for (i = 0; i < count; i++) {
    mu_push_id(ctx, &i, sizeof(i));
    _tmesh_window_row(3, fw);
    mu_label(ctx, files[i].tmesh_config_file_label);
    word = tmesh_config_word(config, files[i].tmesh_config_file_line, files[i].tmesh_config_file_arg);
    text = _tmesh_window_text_field(&_tmesh_window_key_file[i % 64], word);
    if (text != NULL && *text != '\0') {
      tmesh_config_set_word(config, files[i].tmesh_config_file_line, files[i].tmesh_config_file_arg, text);
    }
    if (mu_button(ctx, "Browse...")) {
      _tmesh_window_pick_line = files[i].tmesh_config_file_line;
      _tmesh_window_pick_arg = files[i].tmesh_config_file_arg;
      snprintf(title, sizeof(title), "Choose the %s image", files[i].tmesh_config_file_label);
      _tmesh_window_choose(title, NULL, NULL, FALSE, _tmesh_window_word_chosen, 0);
    }
    _tmesh_window_row(2, sw);
    mu_label(ctx, "");
    _tmesh_window_file_status(&files[i]);
    mu_pop_id(ctx);
  }
  if (files != NULL) {
    tme_free(files);
  }
}

/* the drive whose image is being chosen: */
static int _tmesh_window_pick_drive;

static void
_tmesh_window_drive_chosen(const char *path, int data)
{
  struct tmesh_config_drive *drives;
  char *name;
  int count;

  count = tmesh_config_drives(ui.config, &drives);
  if (path != NULL && _tmesh_window_pick_drive < count) {
    name = tmesh_config_shorten(ui.config, path);
    tmesh_config_drive_set_file(ui.config, &drives[_tmesh_window_pick_drive], name);
    tme_free(name);
    _tmesh_window_edits_clear();
  }
  if (drives != NULL) {
    tme_free(drives);
  }
}

static void
_tmesh_window_page_drives(void)
{
  mu_Context *ctx;
  struct tmesh_config *config;
  struct tmesh_config_drive *drives, *drive;
  const char * const *types;
  const char *text;
  char label[100], value[32];
  int count, n_types, i, sel, kind, removed;
  int dw[6] = { CH(10), CH(6), CH(5), CH(14), -CH(10), -1 };
  int fw[4] = { CH(10), -CH(21), CH(10), -1 };
  int sw[4] = { CH(10), CH(18), CH(8), -1 };
  int aw[3] = { CH(14), CH(14), CH(14) };
  int full[1] = { -1 };

  ctx = ui.ctx;
  config = ui.config;
  count = tmesh_config_drives(config, &drives);
  removed = FALSE;
  _tmesh_window_row(1, full);
  mu_text(ctx, "The SCSI drives.  A disk image that doesn't exist yet is made, empty, "
	  "when the machine is saved.  CD-ROMs and tapes can also be changed "
	  "from the Media menu while the machine runs.");
  for (i = 0; i < count && !removed; i++) {
    drive = &drives[i];
    kind = drive->tmesh_config_drive_kind;
    mu_push_id(ctx, &i, sizeof(i));
    _tmesh_window_row(1, full);
    mu_label(ctx, "");

    /* the kind, SCSI ID and type: */
    _tmesh_window_row(6, dw);
    mu_label(ctx, _tmesh_window_drive_kinds[kind]);
    mu_label(ctx, "ID");
    sel = drive->tmesh_config_drive_id;
    if (_tmesh_window_dropdown("id", _tmesh_window_ids, TME_ARRAY_ELS(_tmesh_window_ids), &sel)) {
      tmesh_config_drive_set_id(config, drive, sel);
    }
    types = tmesh_config_drive_types(kind, &n_types);
    sel = -1;
    for (sel = n_types - 1; sel >= 0 && strcmp(types[sel], drive->tmesh_config_drive_type) != 0; sel--);
    if (_tmesh_window_dropdown("type", (const char **) types, n_types, &sel)) {
      tmesh_config_drive_set_type(config, drive, types[sel]);
    }
    mu_label(ctx, (strcmp(drive->tmesh_config_drive_type, "acb4000") == 0
		   ? "(an Adaptec ACB-4000, for SunOS on a Sun-2 or Sun-3)"
		   : strcmp(drive->tmesh_config_drive_type, "emulex-mt02") == 0
		   ? "(an Emulex MT-02, for SunOS on a Sun-2 or Sun-3)"
		   : ""));
    if (mu_button(ctx, "Remove")) {
      tmesh_config_drive_remove(config, drive);
      _tmesh_window_edits_clear();
      removed = TRUE;
      mu_pop_id(ctx);
      break;
    }

    /* the image: */
    _tmesh_window_row(4, fw);
    mu_label(ctx, (kind == TMESH_CONFIG_DRIVE_TAPE ? "Loaded" : "Image"));
    text = _tmesh_window_text_field(&_tmesh_window_key_drive[i % TMESH_WINDOW_DRIVES],
				    drive->tmesh_config_drive_file);
    if (text != NULL) {
      tmesh_config_drive_set_file(config, drive, text);
      mu_pop_id(ctx);
      break;
    }
    if (mu_button(ctx, "Browse...")) {
      _tmesh_window_pick_drive = i;
      snprintf(label, sizeof(label), "Choose the %s image", _tmesh_window_drive_kinds[kind]);
      _tmesh_window_choose(label, NULL, NULL, FALSE, _tmesh_window_drive_chosen, 0);
    }
    if (kind != TMESH_CONFIG_DRIVE_DISK && mu_button(ctx, "Eject")) {
      tmesh_config_drive_set_file(config, drive, "");
      _tmesh_window_edits_clear();
      mu_pop_id(ctx);
      break;
    }
    else if (kind == TMESH_CONFIG_DRIVE_DISK) {
      mu_label(ctx, "");
    }

    /* the size of a new disk image: */
    if (_tmesh_window_disk_missing(config, drive) && i < TMESH_WINDOW_DRIVES) {
      _tmesh_window_row(4, sw);
      mu_label(ctx, "");
      mu_label(ctx, "New image's size");
      snprintf(value, sizeof(value), "%d", ui.disk_mb[i]);
      text = _tmesh_window_text_field(&ui.disk_mb[i], value);
      if (text != NULL && atoi(text) > 0) {
	ui.disk_mb[i] = atoi(text);
      }
      mu_label(ctx, "MB");
    }
    mu_pop_id(ctx);
  }
  if (drives != NULL) {
    tme_free(drives);
  }

  _tmesh_window_row(1, full);
  mu_label(ctx, "");
  _tmesh_window_row(3, aw);
  for (kind = 0; kind < 3; kind++) {
    snprintf(label, sizeof(label), "Add %s", _tmesh_window_drive_kinds[kind]);
    if (mu_button(ctx, label)) {
      if (tmesh_config_drive_add(config, kind) < 0) {
	_tmesh_window_message("There is no SCSI bus, or no free SCSI ID");
      }
      _tmesh_window_edits_clear();
    }
  }
}

static void
_tmesh_window_page_display(void)
{
  mu_Context *ctx;
  struct tmesh_config *config;
  const struct tmesh_config_kind *kind;
  const char *display, *labels[8];
  int sel, i, n;
  int lw[2] = { CH(22), -1 };
  int full[1] = { -1 };

  ctx = ui.ctx;
  config = ui.config;
  display = tmesh_config_display(config);
  _tmesh_window_row(2, lw);
  mu_label(ctx, "Display");
  if (display == NULL) {
    mu_label(ctx, "None (this script has no display)");
    return;
  }
  {
    static const char *displays[] = { "sdl", "gtk" };
    static const char *display_labels[] = { "SDL", "GTK" };
    sel = -1;
    for (i = 0; i < 2; i++) {
      if (strcmp(display, displays[i]) == 0) {
	sel = i;
      }
    }
    if (_tmesh_window_dropdown("display", display_labels, 2, &sel)) {
      tmesh_config_set_display(config, displays[sel]);
    }
  }

  kind = tmesh_config_kind(config);
  if (kind != NULL && kind->tmesh_config_kind_fbs != NULL) {
    mu_label(ctx, "Framebuffer");
    for (n = 0; kind->tmesh_config_kind_fbs[n].tmesh_config_fb_label != NULL && n < 8; n++) {
      labels[n] = kind->tmesh_config_kind_fbs[n].tmesh_config_fb_label;
    }
    sel = tmesh_config_fb(config);
    if (_tmesh_window_dropdown("fb", labels, n, &sel)) {
      tmesh_config_set_fb(config, sel);
      _tmesh_window_edits_clear();
    }
  }

  _tmesh_window_row(1, full);
  mu_label(ctx, "");
  mu_text(ctx, "The display shows the machine's screen, with menus for scaling it, "
	  "screenshots and recordings, changing media, and the machine's "
	  "switches.  A framebuffer needs its FCode ROM image (see Files).");
}

static void
_tmesh_window_net_field(const char *label, const char *word, int n, const void *key)
{
  const char *value, *text;

  mu_label(ui.ctx, label);
  value = tmesh_config_net_arg(ui.config, word, n);
  text = _tmesh_window_text_field(key, (value != NULL ? value : ""));
  if (text != NULL) {
    tmesh_config_set_net_arg(ui.config, word, n, text);
  }
}

static void
_tmesh_window_page_network(void)
{
  mu_Context *ctx;
  struct tmesh_config *config;
  struct tmesh_config_net net;
  int sel;
  int lw[2] = { CH(22), -1 };
  int full[1] = { -1 };
  static int keys[8];

  ctx = ui.ctx;
  config = ui.config;
  tmesh_config_net(config, &net);
  if (net.tmesh_config_net_line < 0) {
    _tmesh_window_row(1, full);
    mu_label(ctx, "This machine has no Ethernet interface.");
    return;
  }

  _tmesh_window_row(2, lw);
  mu_label(ctx, "Ethernet");
  sel = net.tmesh_config_net_type;
  if (sel == TMESH_CONFIG_NET_OTHER) {
    mu_label(ctx, "Connected (see the Script page)");
  }
  else if (_tmesh_window_dropdown("net", _tmesh_window_nets, TME_ARRAY_ELS(_tmesh_window_nets), &sel)) {
    tmesh_config_set_net(config, sel);
    _tmesh_window_edits_clear();
    tmesh_config_net(config, &net);
  }

  switch (net.tmesh_config_net_type) {
  case TMESH_CONFIG_NET_UDP:
    _tmesh_window_net_field("Listen on address", "listen", 1, &keys[0]);
    _tmesh_window_net_field("and port", "listen", 2, &keys[1]);
    _tmesh_window_net_field("Peer address", "peer", 1, &keys[2]);
    _tmesh_window_net_field("and port", "peer", 2, &keys[3]);
    _tmesh_window_row(1, full);
    mu_label(ctx, "");
    mu_text(ctx, "The interface's frames go over UDP to another emulator (another "
	    "tme, or an emulator with the same Ethernet over UDP), which "
	    "listens on the peer address and port, and sends to this one's.");
    break;
  case TMESH_CONFIG_NET_BPF:
    _tmesh_window_net_field("Host interface", "interface", 1, &keys[4]);
    _tmesh_window_row(1, full);
    mu_label(ctx, "");
    mu_text(ctx, "The interface is bridged to one of the host's (e.g. en0), with BPF.  "
	    "This needs permission to capture packets: read and write access to "
	    "the /dev/bpf devices, or on Linux, the cap_net_raw capability.  "
	    "With no host interface given, BPF chooses one.");
    break;
  case TMESH_CONFIG_NET_TAP:
    _tmesh_window_net_field("Host address", "inet", 1, &keys[5]);
    _tmesh_window_net_field("Netmask", "netmask", 1, &keys[6]);
    _tmesh_window_net_field("Broadcast address", "bcast", 1, &keys[7]);
    _tmesh_window_row(1, full);
    mu_label(ctx, "");
    mu_text(ctx, "The interface is connected to a new tap interface on the host, "
	    "with this address.  Making one needs root, or on Linux, the "
	    "cap_net_admin capability.");
    break;
  case TMESH_CONFIG_NET_NONE:
    _tmesh_window_row(1, full);
    mu_label(ctx, "");
    mu_text(ctx, "The machine has an Ethernet interface, but nothing is connected to it.");
    break;
  }
}

/* this opens the script in a text editor: */
static void
_tmesh_window_open_text(const char *path)
{
  pid_t pid;

  pid = fork();
  if (pid == 0) {
#ifdef __APPLE__
    execlp("open", "open", "-t", path, (char *) NULL);
#else  /* !__APPLE__ */
    execlp("xdg-open", "xdg-open", path, (char *) NULL);
#endif /* !__APPLE__ */
    _exit(127);
  }
  if (pid < 0) {
    _tmesh_window_message("Could not open a text editor: %s", strerror(errno));
  }
}

static void
_tmesh_window_page_script(void)
{
  mu_Context *ctx;
  struct tmesh_config *config;
  int line_i;
  int full[1] = { -1 };
  int bw[2] = { CH(26), -1 };

  ctx = ui.ctx;
  config = ui.config;
  if (config->tmesh_config_path != NULL) {
    _tmesh_window_row(2, bw);
    if (mu_button(ctx, "Open in a text editor")) {
      if (tmesh_config_changed(config)) {
	_tmesh_window_message("Save the machine first: the editor opens the saved script");
      }
      else {
	_tmesh_window_open_text(config->tmesh_config_path);
	_tmesh_window_message("After editing the script, open the machine again with Edit");
      }
    }
  }
  else {
    _tmesh_window_row(1, full);
  }
  mu_label(ctx, "The tmesh commands that make the machine:");

  /* the lines, as closely as text: */
  mu_layout_row(ctx, 1, full, ui.font_h);
  for (line_i = 0; line_i < config->tmesh_config_n_lines; line_i++) {
    mu_label(ctx, config->tmesh_config_lines[line_i].tmesh_config_line_text);
  }
}

static void
_tmesh_window_screen_editor(void)
{
  mu_Context *ctx;
  struct tmesh_config *config;
  char *problem;
  int i;
  int tw[N_PAGES];
  int full[1] = { -1 };
  int bw[4] = { CH(10), -CH(28), CH(10), -1 };
  int rw[3] = { CH(10), -CH(12), -1 };

  ctx = ui.ctx;

  /* the pages: */
  for (i = 0; i < N_PAGES; i++) {
    tw[i] = CH((int) strlen(_tmesh_window_pages[i]) + 4);
  }
  _tmesh_window_row(N_PAGES, tw);
  for (i = 0; i < N_PAGES; i++) {
    mu_push_id(ctx, &i, sizeof(i));
    if (_tmesh_window_list_item(_tmesh_window_pages[i], (int) strlen(_tmesh_window_pages[i]),
				_tmesh_window_pages[i], ui.page == i)) {
      ui.page = i;
    }
    mu_pop_id(ctx);
  }

  mu_layout_row(ctx, 1, full, -_tmesh_window_row_height() * 2 - ctx->style->padding);
  mu_begin_panel(ctx, _tmesh_window_pages[ui.page]);
  switch (ui.page) {
  case PAGE_GENERAL: _tmesh_window_page_general(); break;
  case PAGE_FILES: _tmesh_window_page_files(); break;
  case PAGE_DRIVES: _tmesh_window_page_drives(); break;
  case PAGE_DISPLAY: _tmesh_window_page_display(); break;
  case PAGE_NETWORK: _tmesh_window_page_network(); break;
  case PAGE_SCRIPT: _tmesh_window_page_script(); break;
  }
  mu_end_panel(ctx);

  /* (choosing another kind for a new machine replaces it.) */
  config = ui.config;

  /* a message, or what's wrong, if anything: */
  _tmesh_window_row(1, full);
  problem = tmesh_config_check(config);
  if (ui.chooser_pid > 0) {
    mu_label(ctx, "Waiting for the file chooser...");
  }
  else if (ui.message != NULL) {
    mu_label(ctx, ui.message);
  }
  else if (problem != NULL) {
    char text[1300];
    snprintf(text, sizeof(text), "Not ready: %s", problem);
    mu_label(ctx, text);
  }
  else {
    mu_label(ctx, "Ready");
  }
  if (problem != NULL) {
    tme_free(problem);
  }

  if (ui.running) {
    _tmesh_window_row(3, rw);
  }
  else {
    _tmesh_window_row(4, bw);
  }
  if (mu_button(ctx, (ui.running ? "Close" : "Cancel"))) {
    ui.screen = SCREEN_LIST;
    _tmesh_window_message_clear();
    if (ui.running) {
      ui.quit = TRUE;
    }
  }
  if (config->tmesh_config_path != NULL) {
    _tmesh_window_label_end(config->tmesh_config_path);
  }
  else {
    mu_label(ctx, "(not saved yet)");
  }
  if (ui.running) {
    if (mu_button(ctx, "Save") && _tmesh_window_save(FALSE)) {
      _tmesh_window_message("Saved; the changes take effect when the machine is started again");
    }
    return;
  }
  if (mu_button(ctx, "Save")) {
    _tmesh_window_save(FALSE);
  }
  if (mu_button(ctx, "Save and Start") && _tmesh_window_save(TRUE)) {
    _tmesh_window_start(config);
  }
}

/* the window: */

static void
_tmesh_window_frame(void)
{
  mu_Context *ctx;
  mu_Container *cnt;
  char chunk[32];
  size_t n;
  int full[1] = { -1 };

  ctx = ui.ctx;

  /* text typed or pasted, a little at a time: */
  if (ui.pending_len > 0) {
    n = (ui.pending_len < sizeof(chunk) - 1 ? ui.pending_len : sizeof(chunk) - 1);
    memcpy(chunk, ui.pending_text, n);
    chunk[n] = '\0';
    mu_input_text(ctx, chunk);
    memmove(ui.pending_text, ui.pending_text + n, ui.pending_len - n);
    ui.pending_len -= n;
  }

  mu_begin(ctx);
  cnt = mu_get_container(ctx, "main");
  cnt->rect = mu_rect(0, 0, ui.out_w, ui.out_h);
  if (mu_begin_window_ex(ctx, "main", cnt->rect,
			 MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOCLOSE
			 | MU_OPT_NOFRAME | MU_OPT_NOSCROLL)) {
    _tmesh_window_row(1, full);
    mu_label(ctx, (ui.screen == SCREEN_LIST ? "tme: Machines"
		   : ui.screen == SCREEN_SETTINGS ? "Settings"
		   : tmesh_config_name(ui.config)[0] != '\0' ? tmesh_config_name(ui.config)
		   : "Machine"));
    switch (ui.screen) {
    case SCREEN_LIST: _tmesh_window_screen_list(); break;
    case SCREEN_EDITOR: _tmesh_window_screen_editor(); break;
    case SCREEN_SETTINGS: _tmesh_window_screen_settings(); break;
    }

    /* (the editor shows its own messages.)  */
    if (ui.screen != SCREEN_EDITOR) {
      _tmesh_window_row(1, full);
      mu_label(ctx, (ui.chooser_pid > 0 ? "Waiting for the file chooser..."
		     : ui.message != NULL ? ui.message
		     : ""));
    }
    mu_end_window(ctx);
  }
  mu_end(ctx);
}

static void
_tmesh_window_event(const SDL_Event *event)
{
  mu_Context *ctx;
  SDL_Keycode k;
  char *text;
  size_t n;
  int scale, button, key;

  ctx = ui.ctx;
  scale = ui.scale;
  switch (event->type) {
  case SDL_QUIT:
    ui.quit = TRUE;
    break;
  case SDL_WINDOWEVENT:
    if (event->window.event == SDL_WINDOWEVENT_CLOSE) {
      ui.quit = TRUE;
    }
    if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED
	|| event->window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
      _tmesh_window_make_font();
    }
    break;
  case SDL_MOUSEMOTION:
    mu_input_mousemove(ctx, event->motion.x * scale, event->motion.y * scale);
    break;
  case SDL_MOUSEWHEEL:
    mu_input_scroll(ctx, 0, -event->wheel.y * ui.font_h * 2);
    break;
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:
    button = (event->button.button == SDL_BUTTON_LEFT ? MU_MOUSE_LEFT
	      : event->button.button == SDL_BUTTON_RIGHT ? MU_MOUSE_RIGHT
	      : MU_MOUSE_MIDDLE);
    if (event->type == SDL_MOUSEBUTTONDOWN) {
      mu_input_mousedown(ctx, event->button.x * scale, event->button.y * scale, button);
    }
    else {
      mu_input_mouseup(ctx, event->button.x * scale, event->button.y * scale, button);
    }
    break;
  case SDL_TEXTINPUT:
    n = strlen(event->text.text);
    if (ui.pending_len + n < sizeof(ui.pending_text)) {
      memcpy(ui.pending_text + ui.pending_len, event->text.text, n);
      ui.pending_len += n;
    }
    break;
  case SDL_KEYDOWN:
  case SDL_KEYUP:
    k = event->key.keysym.sym;
    key = (k == SDLK_BACKSPACE ? MU_KEY_BACKSPACE
	   : k == SDLK_RETURN || k == SDLK_KP_ENTER ? MU_KEY_RETURN
	   : k == SDLK_LSHIFT || k == SDLK_RSHIFT ? MU_KEY_SHIFT
	   : k == SDLK_LCTRL || k == SDLK_RCTRL ? MU_KEY_CTRL
	   : k == SDLK_LALT || k == SDLK_RALT ? MU_KEY_ALT
	   : 0);
    if (key != 0) {
      if (event->type == SDL_KEYDOWN) {
	mu_input_keydown(ctx, key);
      }
      else {
	mu_input_keyup(ctx, key);
      }
    }

    /* paste (Ctrl-V, or Cmd-V on macOS): */
    if (event->type == SDL_KEYDOWN && k == SDLK_v
	&& (event->key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
      text = SDL_GetClipboardText();
      if (text != NULL) {
	n = strlen(text);
	if (ui.pending_len + n < sizeof(ui.pending_text)) {
	  memcpy(ui.pending_text + ui.pending_len, text, n);
	  ui.pending_len += n;
	}
	SDL_free(text);
      }
    }

    /* Escape closes a drop-down list, or goes back to the list: */
    if (event->type == SDL_KEYDOWN && k == SDLK_ESCAPE) {
      if (ui.open_list != NULL && ui.open_list->open) {
	ui.open_list->open = 0;
      }
      else if (ui.screen != SCREEN_LIST && ui.chooser_pid <= 0) {
	ui.screen = SCREEN_LIST;
	if (ui.running) {
	  ui.quit = TRUE;
	}
      }
    }
    break;
  }
}

/* this runs the window.  with a script, it opens the editor for it
   (for a running machine, with running nonzero), or if the script
   doesn't exist, for a new machine.  it returns FALSE if the window
   can't open, and otherwise the script of the machine to start, or
   NULL: */
int
tmesh_window_run(const char *edit_path, int running, char **_script)
{
  struct tmesh_config *config;
  SDL_Event event;
  struct stat st;
  char *err;

  memset(&ui, 0, sizeof(ui));
  *_script = NULL;

  SDL_SetMainReady();
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "tmesh: could not open the machine window: %s\n", SDL_GetError());
    return (FALSE);
  }
  ui.window = SDL_CreateWindow("tme", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			       TMESH_WINDOW_W, TMESH_WINDOW_H,
			       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (ui.window != NULL) {
    ui.renderer = SDL_CreateRenderer(ui.window, -1, SDL_RENDERER_PRESENTVSYNC);
  }
  if (ui.renderer == NULL) {
    fprintf(stderr, "tmesh: could not open the machine window: %s\n", SDL_GetError());
    if (ui.window != NULL) {
      SDL_DestroyWindow(ui.window);
    }
    SDL_Quit();
    return (FALSE);
  }
  SDL_SetWindowMinimumSize(ui.window, 640, 420);

  /* come to the front.  the window of a new process (like the editor
     that a running machine's Edit Configuration... starts) would
     otherwise open behind the active one, at least on macOS: */
  SDL_RaiseWindow(ui.window);
#ifdef __APPLE__
  {
    id app;

    /* [NSApp activateIgnoringOtherApps:YES], since tmesh isn't an
       .app that macOS would activate when it starts: */
    app = ((id (*)(Class, SEL)) objc_msgSend)(objc_getClass("NSApplication"),
					      sel_registerName("sharedApplication"));
    ((void (*)(id, SEL, BOOL)) objc_msgSend)(app, sel_registerName("activateIgnoringOtherApps:"), YES);
  }
#endif /* __APPLE__ */
  SDL_StartTextInput();

  ui.ctx = tme_new0(mu_Context, 1);
  mu_init(ui.ctx);
  ui.ctx->text_width = _tmesh_window_text_width;
  ui.ctx->text_height = _tmesh_window_text_height;
  _tmesh_window_set_style();
  _tmesh_window_make_font();
  _tmesh_window_list_load(NULL);

  if (edit_path != NULL) {
    if (stat(edit_path, &st) == 0) {
      config = tmesh_config_load(edit_path, &err);
      if (config == NULL) {
	fprintf(stderr, "tmesh: %s\n", err);
	tme_free(err);
	ui.quit = running;
      }
      else {
	_tmesh_window_open_editor(config, FALSE);
      }
    }
    else {
      _tmesh_window_new_kind(0, NULL);
    }
  }
  if (running && ui.screen == SCREEN_EDITOR) {
    ui.running = TRUE;
    _tmesh_window_message("The machine is running: the changes take effect when it's started again");
  }

  while (!ui.quit && ui.result == NULL) {
    if (SDL_WaitEventTimeout(&event, 50)) {
      _tmesh_window_event(&event);
      while (SDL_PollEvent(&event)) {
	_tmesh_window_event(&event);
      }
    }
    _tmesh_window_choose_poll();
    _tmesh_window_frame();
    _tmesh_window_render();
  }

  if (ui.chooser_pid > 0) {
    kill(ui.chooser_pid, SIGTERM);
    close(ui.chooser_fd);
    waitpid(ui.chooser_pid, NULL, 0);
  }
  _tmesh_window_list_free();
  tmesh_config_list_free(ui.n_rom_dirs, ui.rom_dirs);
  tmesh_config_free(ui.config);
  _tmesh_window_message_clear();
  tme_free(ui.ctx);
  if (ui.font != NULL) {
    SDL_DestroyTexture(ui.font);
  }
  SDL_DestroyRenderer(ui.renderer);
  SDL_DestroyWindow(ui.window);
  SDL_StopTextInput();
  SDL_Quit();
  *_script = ui.result;
  return (TRUE);
}
