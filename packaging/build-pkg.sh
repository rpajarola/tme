#!/usr/bin/env bash
# Builds a macOS installer package, on macOS with the Xcode command line
# tools and Homebrew's build dependencies (see packaging/README.md). GTK,
# SDL and the libraries they need go into the package, with the GTK data
# tmesh uses, so it runs without Homebrew.
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
brew=$(brew --prefix)

if [ ! -x configure ]; then
  autoreconf -fi
fi
# Room in the load commands for the bundled libraries' paths:
./configure --prefix=/usr/local LDFLAGS="-Wl,-headerpad_max_install_names"
make -j"$(sysctl -n hw.ncpu)"
make install DESTDIR="$work/stage"

# The modules are linked into tmesh: the package needs only the programs,
# the examples and the manual.
s="$work/stage/usr/local"
install -d "$root/usr/local/bin" "$root/usr/local/libexec/tme" "$root/usr/local/share/info" \
  "$root/usr/local/share/doc/tme"
for p in "$s"/bin/*; do
  case "$(basename "$p")" in
    tmesh) install -m 0755 "$p" "$root/usr/local/libexec/tme/tmesh" ;;
    *) install -m 0755 "$p" "$root/usr/local/bin/" ;;
  esac
done
install -m 0755 packaging/macos/tmesh.sh "$root/usr/local/bin/tmesh"
cp -R "$s/share/tme" "$root/usr/local/share/tme"
cp "$s/share/info/tme.info" "$root/usr/local/share/info/"
cp README NEWS COPYING "$root/usr/local/share/doc/tme/"
install -m 0755 packaging/macos/uninstall.sh "$root/usr/local/share/tme/uninstall.sh"

packaging/bundle-dylibs.sh "$root/usr/local/lib/tme" "$root/usr/local/libexec/tme/tmesh"
# Homebrew's SDL 2 is sdl2-compat, which loads SDL 3 when it is loaded, as
# @loader_path/libSDL3.dylib:
lib="$root/usr/local/lib/tme"
if [ -e "$lib/libSDL2-2.0.0.dylib" ]; then
  cp "$(brew --prefix sdl3)/lib/libSDL3.0.dylib" "$lib/libSDL3.dylib"
  chmod u+w "$lib/libSDL3.dylib"
  install_name_tool -id @loader_path/libSDL3.dylib "$lib/libSDL3.dylib" 2>/dev/null
  packaging/bundle-dylibs.sh "$lib" "$lib/libSDL3.dylib"
fi

# GTK's data: the settings schemas its dialogs need, and empty module
# caches, so it doesn't look for loaders and input methods in Homebrew.
gtk="$root/usr/local/share/tme/gtk"
install -d "$gtk/glib-2.0/schemas"
cp "$brew"/share/glib-2.0/schemas/org.gtk.Settings.*.gschema.xml "$gtk/glib-2.0/schemas/"
glib-compile-schemas "$gtk/glib-2.0/schemas"
: > "$gtk/loaders.cache"
: > "$gtk/immodules.cache"
cp packaging/macos/fonts.conf "$gtk/fonts.conf"

pkgbuild --root "$root" --identifier io.github.rpajarola.tme --version "$VERSION" \
  --install-location / --ownership recommended "$work/tme.pkg"
mkdir -p dist
out="dist/tme-$VERSION-$(uname -m).pkg"
sign=()
[ -n "${PKG_SIGN_IDENTITY:-}" ] && sign=(--sign "$PKG_SIGN_IDENTITY")
productbuild --package "$work/tme.pkg" ${sign[@]+"${sign[@]}"} "$out"
ls -l "$out"
