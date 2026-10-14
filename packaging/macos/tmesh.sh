#!/bin/sh
# Runs tmesh with the GTK libraries and data bundled in the package.
gtk=/usr/local/share/tme/gtk
export XDG_DATA_DIRS="$gtk${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export GSETTINGS_SCHEMA_DIR="$gtk/glib-2.0/schemas"
export GDK_PIXBUF_MODULE_FILE="$gtk/loaders.cache"
export GTK_IM_MODULE_FILE="$gtk/immodules.cache"
export FONTCONFIG_FILE="$gtk/fonts.conf"
exec /usr/local/libexec/tme/tmesh "$@"
