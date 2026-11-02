# Packages

tme is packaged as `tme-rp` (called `tme` before 1.0.0): a `.deb` for
Debian and Ubuntu (amd64 and arm64, built on Ubuntu 24.04, published in
the shared apt repository [rpajarola/apt](https://github.com/rpajarola/apt)),
a `.pkg` for macOS, a Homebrew formula in
[rpajarola/homebrew-tap](https://github.com/rpajarola/homebrew-tap), and a
zip for Windows.
The `.deb` conflicts with, replaces and provides `tme`, so it replaces
an installed `tme` package. The programs keep their names (`tmesh` and
the `tme-*` tools).
Publishing a GitHub release builds all of them
([`.github/workflows/release.yml`](../.github/workflows/release.yml)).

## Install

**Ubuntu and Debian**, from the [rpajarola apt repository](https://rpajarola.github.io/apt),
which also has the other packages built this way:

```
sudo curl -fsSLo /usr/share/keyrings/rpajarola.gpg https://rpajarola.github.io/apt/rpajarola.gpg
echo "deb [signed-by=/usr/share/keyrings/rpajarola.gpg] https://rpajarola.github.io/apt stable main" \
  | sudo tee /etc/apt/sources.list.d/rpajarola.list
sudo apt update
sudo apt install tme-rp
```

or a downloaded package: `sudo apt install ./tme-rp_<version>_<arch>.deb`.

**macOS** with Homebrew:

```
brew install rpajarola/tap/tme-rp
```

or the `.pkg` from a release: `sudo installer -pkg tme-rp-<version>-arm64.pkg -target /`
(the package is not signed, so double-clicking it needs right-click → Open).

**Windows** (10 and later, 64-bit): unpack `tme-rp-<version>-windows-x86_64.zip`
from a release anywhere, and run `tmesh.exe` from it. It is not signed,
so SmartScreen asks before it runs the first time.

## What is installed

Debian and Ubuntu:

```
/usr/bin/tmesh, tme-sun-idprom, tme-sun-eeprom, tme-sunlabel, tme-mktap
/usr/share/tme/examples/        # SUN2-MULTIBUS, SUN3-CARRERA, SUN4-75, SUN-ULTRA-1, keyboard maps
/usr/share/info/tme.info.gz
/usr/share/applications/tme.desktop
/usr/share/icons/hicolor/*/apps/tme.{png,svg}
```

The modules are linked into tmesh, so the package leaves out the
libraries, modules and headers that `make install` also installs. Its
dependencies are worked out by `dpkg-shlibdeps` when it is built.

macOS (`.pkg`):

```
/usr/local/bin/tmesh, tme-sun-idprom, tme-sun-eeprom, tme-sunlabel, tme-mktap
/usr/local/lib/tme/                  # SDL and the libraries it needs, from Homebrew
/usr/local/share/tme/examples/
/usr/local/share/tme/uninstall.sh
```

The package runs without Homebrew: `packaging/bundle-dylibs.sh` copies
the libraries tmesh needs from Homebrew into it and points tmesh at the
copies. It is built for the Mac's architecture (Apple silicon in CI);
on Intel Macs, use Homebrew. Remove it with
`sudo /usr/local/share/tme/uninstall.sh`.

Windows (zip):

```
tme-rp-<version>-windows-x86_64/
  tmesh.exe, tme-sun-idprom.exe, tme-sun-eeprom.exe, tme-sunlabel.exe, tme-mktap.exe
  SDL2.dll                  # the official SDL 2 build for mingw
  examples/
  doc/                      # tme.html, tme.info, README, NEWS, COPYING, SDL2's license
```

The modules are linked into tmesh.exe, and so is zlib; the build checks
that the programs need no other DLLs than Windows' own and SDL2.dll.

## The icon

tme's icon (a robot emu whose body is a SPARCstation IPC-like
lunchbox) is drawn in `icon/tme-icon.svg`. `icon/make-icons.sh` (which
needs rsvg-convert and ImageMagick) makes the rest from it, which is in
git: the PNGs of the icon theme, and `host/display/sdl/sdl-icon.h`, which
tmesh gives the machine window and the display's windows (and on macOS,
the Dock). `icon/tme.desktop` gives it to Wayland's task switchers, which
go by the app ID (`tmesh`), not by the windows' icons.

## Build

Debian or Ubuntu:

```
sudo apt install build-essential autoconf automake libtool libltdl-dev pkg-config \
  bison texinfo libsdl2-dev zlib1g-dev dpkg-dev
packaging/build-deb.sh      # dist/tme-rp_<version>_<arch>.deb
```

macOS:

```
brew install autoconf automake libtool pkgconf texinfo sdl2
packaging/build-pkg.sh      # dist/tme-rp-<version>-<arch>.pkg
```

Windows, in an MSYS2 UCRT64 shell:

```
pacman -S autoconf automake libtool make bison texinfo curl zip \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib
packaging/build-win.sh      # dist/tme-rp-<version>-windows-x86_64.zip
```

or cross-compiled, e.g. on macOS with `brew install mingw-w64` and Wine
(which runs the programs that make sources during the build, and the
checks at the end; set `CROSS_RUN` for another command). It downloads
SDL2 (or set `SDL2_MINGW` to an unpacked `SDL2-devel-<version>-mingw`), and
when cross-compiling, builds zlib. Like the other scripts, it configures
and builds in the source tree.

Both need nfpm for the `.deb` (`go install github.com/goreleaser/nfpm/v2/cmd/nfpm@latest`,
or a release binary). The version is the git tag without its `v`, or
`<configure version>~git<sha>`; set `VERSION` to override it.

## Releasing

Tag, then publish a GitHub release for the tag:

```
git tag v1.0.0 && git push origin v1.0.0
gh release create v1.0.0 -R rpajarola/tme --generate-notes
```

(`-R`, since this is a fork, and gh would otherwise use phabrics/tme.)

The release workflow then attaches the source tarball (`make dist`), the
`.deb`s, the `.pkg` and the Windows zip, has
[rpajarola/apt](https://github.com/rpajarola/apt) rebuild the apt
repository (from the `.deb`s of every release of every project in it),
and updates the formula's URL and checksum in
[rpajarola/homebrew-tap](https://github.com/rpajarola/homebrew-tap). To
rebuild a release's packages, run the workflow by hand with its tag.

One-time setup: a `PACKAGES_TOKEN` secret, a fine-grained token with
contents read and write on `rpajarola/homebrew-tap` and `rpajarola/apt`
(the same token in every project):

```
gh secret set PACKAGES_TOKEN
```

Without it, the release still gets its packages, but the formula is not
updated and the apt repository is only rebuilt by its weekly run (or by
hand: Actions → publish → Run workflow in rpajarola/apt). The signing key
lives in rpajarola/apt.
