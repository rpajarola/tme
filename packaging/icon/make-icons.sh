#!/usr/bin/env bash
# Makes tme's icons from tme-icon.svg (a robot emu whose body is a
# SPARCstation IPC-like lunchbox).  Needs rsvg-convert (librsvg) and
# ImageMagick; the results are in git, so that building tme doesn't:
#
#	png/tme-N.png				N = 16 ... 256, for Linux's icon theme
#	../../host/display/sdl/sdl-icon.h	the window icon (256 pixels)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
mkdir -p png
for n in 16 24 32 48 64 128 256; do
	rsvg-convert -w "$n" -h "$n" tme-icon.svg -o "png/tme-$n.png"
done

# The window icon, as runs of equal 32-bit pixels (0xAARRGGBB):
magick png/tme-256.png -depth 8 rgba:- | python3 -c '
import sys
d = sys.stdin.buffer.read()
runs = []
for i in range(0, len(d), 4):
    r, g, b, a = d[i:i + 4]
    p = (a << 24) | (r << 16) | (g << 8) | b
    if runs and runs[-1][1] == p and runs[-1][0] < 255:
        runs[-1][0] += 1
    else:
        runs.append([1, p])
out = sys.stdout
out.write("/* made by packaging/icon/make-icons.sh from tme-icon.svg. */\n\n")
out.write("#ifndef _HOST_DISPLAY_SDL_SDL_ICON_H\n#define _HOST_DISPLAY_SDL_SDL_ICON_H\n\n")
out.write("#include <SDL.h>\n\n")
out.write("#define TME_SDL_ICON_SIZE\t(256)\n#define TME_SDL_ICON_RUNS\t(%d)\n\n" % len(runs))
out.write("/* runs of equal pixels: count, 0xAARRGGBB: */\n")
out.write("static const struct { unsigned char n; Uint32 argb; } _tme_sdl_icon[] = {\n")
for i in range(0, len(runs), 4):
    out.write("  " + " ".join("{%d,0x%08x}," % (n, p) for n, p in runs[i:i + 4]) + "\n")
out.write("};\n\n")
out.write("""/* gives a window the tme icon.  on macOS, this is also the
   icon of the application, in the Dock and the switcher: */
static void
_tme_sdl_set_icon(SDL_Window *window)
{
  static SDL_Surface *icon = NULL;
  unsigned long run, pixel, count;
  Uint32 *row;

  if (icon == NULL) {
    icon = SDL_CreateRGBSurfaceWithFormat(0, TME_SDL_ICON_SIZE, TME_SDL_ICON_SIZE,
					  32, SDL_PIXELFORMAT_ARGB8888);
    if (icon == NULL) {
      return;
    }
    pixel = 0;
    for (run = 0; run < TME_SDL_ICON_RUNS; run++) {
      for (count = 0; count < _tme_sdl_icon[run].n; count++, pixel++) {
	row = (Uint32 *) ((Uint8 *) icon->pixels
			  + (pixel / TME_SDL_ICON_SIZE) * icon->pitch);
	row[pixel % TME_SDL_ICON_SIZE] = _tme_sdl_icon[run].argb;
      }
    }
  }
  SDL_SetWindowIcon(window, icon);
}

#endif /* !_HOST_DISPLAY_SDL_SDL_ICON_H */
""")
' > ../../host/display/sdl/sdl-icon.h
ls -l png ../../host/display/sdl/sdl-icon.h
