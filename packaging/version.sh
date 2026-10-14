# Sourced by the build scripts: sets VERSION from the environment, the git
# tag (without its v), or <configure version>~git<sha>.
if [ -z "${VERSION:-}" ]; then
  if tag=$(git describe --tags --exact-match 2>/dev/null); then
    VERSION="${tag#v}"
  else
    base=$(sed -n 's/^AC_INIT(\[tme\], \[\(.*\)\])/\1/p' configure.ac)
    VERSION="${base}~git$(git rev-parse --short HEAD)"
  fi
fi
