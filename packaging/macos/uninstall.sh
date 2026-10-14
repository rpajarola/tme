#!/bin/sh
# Removes tme installed from the .pkg:
#   sudo /usr/local/share/tme/uninstall.sh
set -e
if [ "$(id -u)" != 0 ]; then
  echo "run with sudo" >&2
  exit 1
fi
rm -f /usr/local/bin/tmesh /usr/local/bin/tme-mktap /usr/local/bin/tme-sun-eeprom \
  /usr/local/bin/tme-sun-idprom /usr/local/bin/tme-sunlabel /usr/local/share/info/tme.info
rm -rf /usr/local/libexec/tme /usr/local/lib/tme /usr/local/share/tme /usr/local/share/doc/tme
pkgutil --forget io.github.rpajarola.tme >/dev/null 2>&1 || true
echo "tme removed"
