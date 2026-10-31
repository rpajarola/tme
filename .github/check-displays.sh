#!/usr/bin/env bash
# Checks that configure chose exactly the displays given, and that tmesh
# can load each of them. Run from the top of a configured and built tree:
#
#   .github/check-displays.sh rfb sdl
#   .github/check-displays.sh rfb          # without SDL
#
# SDL uses its dummy driver here, so no window system is needed. LOAD
# lists the displays to load, if not all of them:
#
#   LOAD=sdl .github/check-displays.sh rfb sdl
set -euo pipefail

want=$(printf '%s\n' "$@" | sort | xargs)
got=$(sed -n 's/^TME_DISPLAY_HOSTS = //p' host/display/Makefile | tr ' ' '\n' | sort | xargs)
if [ "$got" != "$want" ]; then
  echo "check-displays.sh: configure chose displays \"$got\", not \"$want\"" >&2
  exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for display in ${LOAD-$@}; do
  echo "display0: tme/host/$display/display" > "$work/display.cfg"
  status=0
  out=$(echo "source $work/display.cfg" | SDL_VIDEODRIVER=dummy tmesh/tmesh 2>&1) || status=$?
  echo "$out"
  # tmesh reports a config line it couldn't run as file:line: error:
  if [ "$status" -ne 0 ] || echo "$out" | grep -q 'display\.cfg:'; then
    echo "check-displays.sh: tmesh couldn't load the $display display" >&2
    exit 1
  fi
done
echo "check-displays.sh: displays \"$want\" OK"
