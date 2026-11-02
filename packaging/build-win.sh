#!/usr/bin/env bash
# Builds a zip of tme for 64-bit Windows (10 and later), with the SDL
# display: tmesh.exe (with all of the modules in it), the tools, SDL2.dll,
# the examples and the manual (as HTML, and info).
#
# Natively, in an MSYS2 UCRT64 shell (pacman -S autoconf automake libtool
# make bison texinfo zip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib),
# or cross-compiled with mingw-w64 elsewhere (brew install mingw-w64, or
# apt install gcc-mingw-w64-x86-64), where the programs that make sources
# at build time run with $CROSS_RUN (default: wine), and zlib is built
# from source:
#
#   packaging/build-win.sh                 # version from git
#   VERSION=1.2.3 packaging/build-win.sh
#
# SDL2 is the official mingw build, downloaded (or $SDL2_MINGW, an
# unpacked SDL2-devel-<version>-mingw directory).
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
. packaging/version.sh

SDL2_VERSION=2.32.10
SDL2_SHA256=83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2
ZLIB_VERSION=1.3.1
ZLIB_SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sha256() {
  echo "$1  $2" | sha256sum -c - 2>/dev/null || echo "$1  $2" | shasum -a 256 -c -
}

host=x86_64-w64-mingw32
configure_args=()
case "$(uname -s)" in
  MINGW*|MSYS*)
    run=()
    ;;
  *)
    export CC="${CC:-$host-gcc}"
    export CROSS_RUN="${CROSS_RUN:-wine}"
    if ! command -v "${CROSS_RUN%% *}" > /dev/null; then
      echo "Set CROSS_RUN to a command that runs Windows programs (e.g. wine)" >&2
      exit 1
    fi
    read -r -a run <<< "$CROSS_RUN"
    configure_args+=(--host="$host" CROSS_RUN="$CROSS_RUN")

    # zlib, built here, and linked into tmesh.exe:
    tarball="zlib-$ZLIB_VERSION.tar.gz"
    curl -fsSL -o "$work/$tarball" "https://zlib.net/fossils/$tarball"
    sha256 "$ZLIB_SHA256" "$work/$tarball"
    tar xzf "$work/$tarball" -C "$work"
    make -s -C "$work/zlib-$ZLIB_VERSION" -f win32/Makefile.gcc \
      PREFIX="$host-" libz.a
    configure_args+=(CPPFLAGS="-I$work/zlib-$ZLIB_VERSION"
                     LDFLAGS="-L$work/zlib-$ZLIB_VERSION")
    ;;
esac
OBJDUMP="${OBJDUMP:-${CC:-gcc}}"
OBJDUMP="${OBJDUMP%gcc}objdump"

if [ -z "${SDL2_MINGW:-}" ]; then
  tarball="SDL2-devel-$SDL2_VERSION-mingw.tar.gz"
  curl -fsSL -o "$work/$tarball" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/$tarball"
  sha256 "$SDL2_SHA256" "$work/$tarball"
  tar xzf "$work/$tarball" -C "$work"
  SDL2_MINGW="$work/SDL2-$SDL2_VERSION"
fi
sdl="$SDL2_MINGW/$host"

if [ ! -x configure ]; then
  autoreconf -fi
fi
# The package has the SDL display, so configure must find SDL, instead of
# leaving the display out.  (SDL's own sdl2.pc has the paths of the
# machine it was built on.)
./configure --prefix=/ --disable-shared --enable-static \
  --enable-hosts="sdl udp rfb" \
  SDL_CFLAGS="-I$sdl/include/SDL2" SDL_LIBS="-L$sdl/lib -lSDL2" \
  ${configure_args[@]+"${configure_args[@]}"}
grep -q '^#define HAVE_ZLIB 1' config.h
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
make install DESTDIR="$work/stage"

# Only Windows' own DLLs and SDL2.dll may be needed:
for exe in "$work"/stage/bin/*.exe; do
  "$OBJDUMP" -p "$exe" | sed -n 's/^.*DLL Name: //p'
done | sort -u > "$work/dlls"
if grep -viE '^(advapi32|comdlg32|gdi32|imm32|kernel32|ole32|oleaut32|setupapi|shell32|user32|version|winmm|ws2_32|sdl2|api-ms-win-crt-[a-z0-9-]+)\.dll$' "$work/dlls"; then
  echo "the programs need the DLLs above, which aren't in the zip" >&2
  exit 1
fi

name="tme-rp-$VERSION-windows-x86_64"
dir="$work/$name"
mkdir -p "$dir/doc"
cp "$work"/stage/bin/*.exe "$sdl/bin/SDL2.dll" "$dir/"
cp -R "$work/stage/share/tme/examples" "$dir/examples"
cp "$work/stage/share/info/tme.info" "$dir/doc/"
makeinfo --html --no-split -o "$dir/doc/tme.html" tme/tme.texi
for f in README NEWS COPYING; do cp "$f" "$dir/doc/$f.txt"; done
cp "$SDL2_MINGW/LICENSE.txt" "$dir/doc/SDL2-LICENSE.txt"

# tmesh --help gives the usage, and the SDL and VNC displays load:
usage=$(cd "$dir" && ${run[@]+"${run[@]}"} ./tmesh.exe --help 2>&1 || true)
grep -q 'usage: tmesh ' <<< "$usage"
for display in sdl rfb; do
  echo "display0: tme/host/$display/display" > "$work/display.cfg"
  out=$(cd "$work" && echo 'source display.cfg' |
          SDL_VIDEODRIVER=dummy ${run[@]+"${run[@]}"} "$dir/tmesh.exe" --shell 2>&1 || true)
  echo "$out"
  if grep -q 'display\.cfg:' <<< "$out"; then
    echo "tmesh.exe couldn't load the $display display" >&2
    exit 1
  fi
done

mkdir -p dist
out="$PWD/dist/$name.zip"
rm -f "$out"
(cd "$work" && zip -qr "$out" "$name")
ls -l "$out"
