#!/usr/bin/env bash
# Builds a macOS installer package, on macOS with the Xcode command line
# tools and Homebrew's build dependencies (see packaging/README.md). SDL
# and the libraries it needs go into the package, so it runs without
# Homebrew.
#
#   packaging/build-pkg.sh                 # version from git
#   VERSION=1.2.3 packaging/build-pkg.sh
#
# The package is for the architecture of the Homebrew it was built with.
# Set PKG_SIGN_IDENTITY to a "Developer ID Installer" identity to sign it.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
. packaging/version.sh

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root="$work/root"

if [ ! -x configure ]; then
  autoreconf -fi
fi
# Room in the load commands for the bundled libraries' paths.  The
# package has the SDL display, so configure must find SDL, instead of
# leaving the display out:
./configure --prefix=/usr/local LDFLAGS="-Wl,-headerpad_max_install_names" \
  --enable-hosts="rfb sdl tun bsd udp"
make -j"$(sysctl -n hw.ncpu)"
make install DESTDIR="$work/stage"

# The modules are linked into tmesh: the package needs only the programs,
# the examples and the manual.
s="$work/stage/usr/local"
install -d "$root/usr/local/bin" "$root/usr/local/share/info" \
  "$root/usr/local/share/doc/tme"
install -m 0755 "$s"/bin/* "$root/usr/local/bin/"
cp -R "$s/share/tme" "$root/usr/local/share/tme"
cp "$s/share/info/tme.info" "$root/usr/local/share/info/"
cp README NEWS COPYING "$root/usr/local/share/doc/tme/"
install -m 0755 packaging/macos/uninstall.sh "$root/usr/local/share/tme/uninstall.sh"

packaging/bundle-dylibs.sh "$root/usr/local/lib/tme" "$root/usr/local/bin/tmesh"
# Homebrew's SDL 2 is sdl2-compat, which loads SDL 3 when it is loaded, as
# @loader_path/libSDL3.dylib:
lib="$root/usr/local/lib/tme"
if [ -e "$lib/libSDL2-2.0.0.dylib" ]; then
  cp "$(brew --prefix sdl3)/lib/libSDL3.0.dylib" "$lib/libSDL3.dylib"
  chmod u+w "$lib/libSDL3.dylib"
  install_name_tool -id @loader_path/libSDL3.dylib "$lib/libSDL3.dylib" 2>/dev/null
  packaging/bundle-dylibs.sh "$lib" "$lib/libSDL3.dylib"
fi

pkgbuild --root "$root" --identifier io.github.rpajarola.tme --version "$VERSION" \
  --install-location / --ownership recommended "$work/tme.pkg"
mkdir -p dist
out="dist/tme-rp-$VERSION-$(uname -m).pkg"
sign=()
[ -n "${PKG_SIGN_IDENTITY:-}" ] && sign=(--sign "$PKG_SIGN_IDENTITY")
productbuild --package "$work/tme.pkg" ${sign[@]+"${sign[@]}"} "$out"
ls -l "$out"
