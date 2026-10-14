# Packages

tme is packaged as a `.deb` for Debian and Ubuntu (amd64 and arm64,
built on Ubuntu 24.04, published in the shared apt repository
[rpajarola/apt](https://github.com/rpajarola/apt)), a `.pkg` for macOS, and a Homebrew formula in
[rpajarola/homebrew-tap](https://github.com/rpajarola/homebrew-tap).
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
sudo apt install tme
```

or a downloaded package: `sudo apt install ./tme_<version>_<arch>.deb`.

**macOS** with Homebrew:

```
brew install rpajarola/tap/tme
```

or the `.pkg` from a release: `sudo installer -pkg tme-<version>-arm64.pkg -target /`
(the package is not signed, so double-clicking it needs right-click → Open).

## What is installed

Debian and Ubuntu:

```
/usr/bin/tmesh, tme-sun-idprom, tme-sun-eeprom, tme-sunlabel, tme-mktap
/usr/share/tme/examples/        # SUN2-MULTIBUS, SUN3-CARRERA, SUN4-75, SUN-ULTRA-1, keyboard maps
/usr/share/info/tme.info.gz
```

The modules are linked into tmesh, so the package leaves out the
libraries, modules and headers that `make install` also installs. Its
dependencies are worked out by `dpkg-shlibdeps` when it is built.

macOS (`.pkg`):

```
/usr/local/bin/tmesh                 # sets up the bundled GTK, runs libexec/tme/tmesh
/usr/local/bin/tme-sun-idprom, tme-sun-eeprom, tme-sunlabel, tme-mktap
/usr/local/libexec/tme/tmesh
/usr/local/lib/tme/                  # GTK, SDL and their libraries, from Homebrew
/usr/local/share/tme/examples/
/usr/local/share/tme/gtk/            # GTK settings schemas, empty module caches, fonts.conf
/usr/local/share/tme/uninstall.sh
```

The package runs without Homebrew: `packaging/bundle-dylibs.sh` copies
the libraries tmesh needs from Homebrew into it and points tmesh at the
copies. It is built for the Mac's architecture (Apple silicon in CI);
on Intel Macs, use Homebrew. Remove it with
`sudo /usr/local/share/tme/uninstall.sh`.

## Build

Debian or Ubuntu:

```
sudo apt install build-essential autoconf automake libtool libltdl-dev pkg-config \
  bison texinfo libgtk-3-dev libsdl2-dev dpkg-dev
packaging/build-deb.sh      # dist/tme_<version>_<arch>.deb
```

macOS:

```
brew install autoconf automake libtool pkgconf texinfo gtk+3 sdl2
packaging/build-pkg.sh      # dist/tme-<version>-<arch>.pkg
```

Both need nfpm for the `.deb` (`go install github.com/goreleaser/nfpm/v2/cmd/nfpm@latest`,
or a release binary). The version is the git tag without its `v`, or
`<configure version>~git<sha>`; set `VERSION` to override it.

## Releasing

Tag, then publish a GitHub release for the tag:

```
git tag v0.12rc7 && git push origin v0.12rc7
gh release create v0.12rc7 -R rpajarola/tme --generate-notes
```

(`-R`, since this is a fork, and gh would otherwise use phabrics/tme.)

The release workflow then attaches the source tarball (`make dist`), the
`.deb`s and the `.pkg`, has
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
