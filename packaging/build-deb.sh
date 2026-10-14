#!/usr/bin/env bash
# Builds the .deb for this machine's architecture, on Debian or Ubuntu with
# the build dependencies installed (see packaging/README.md) and nfpm
# (https://nfpm.goreleaser.com).
#
#   packaging/build-deb.sh                 # version from git
#   VERSION=1.2.3 packaging/build-deb.sh
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
. packaging/version.sh

for tool in nfpm dpkg-shlibdeps; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "build-deb.sh: $tool not found; see packaging/README.md" >&2
    exit 1
  fi
done
ARCH="${ARCH:-$(dpkg --print-architecture)}"

if [ ! -x configure ]; then
  autoreconf -fi
fi
./configure --prefix=/usr --sysconfdir=/etc
make -j"$(nproc)"
rm -rf dist/stage
make install DESTDIR="$PWD/dist/stage"
gzip -9n dist/stage/usr/share/info/tme.info

# The modules are linked into tmesh, so the package needs only the
# programs, the examples and the manual: not the libraries, modules and
# headers that make install also puts there.
bins=$(ls "$PWD"/dist/stage/usr/bin/*)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/debian"
printf 'Source: tme\n\nPackage: tme\nArchitecture: any\n' > "$work/debian/control"
depends=$(cd "$work" && dpkg-shlibdeps -O -e $bins | sed -n 's/^shlibs:Depends=//p')
{
  cat packaging/nfpm.yaml
  echo "depends:"
  echo "$depends" | tr ',' '\n' | sed -e 's/^ *//' -e 's/^/  - "/' -e 's/$/"/'
} > "$work/nfpm.yaml"

VERSION="$VERSION" ARCH="$ARCH" nfpm pkg --config "$work/nfpm.yaml" --packager deb \
  --target "dist/tme_${VERSION}_${ARCH}.deb"
ls -l "dist/tme_${VERSION}_${ARCH}.deb"
