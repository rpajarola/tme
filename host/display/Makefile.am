## Process this file with automake to produce Makefile.in
# Makefile.am for The Machine Emulator display/:

AUTOMAKE_OPTIONS = 1.4 gnu
AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir) -I$(top_srcdir)/lib -I. -D_TME_IMPL $(GTK_CFLAGS)

lib_LTLIBRARIES = libtme-display.la
libtme_display_la_SOURCES = display-mouse.c display-keyboard.c display.c display.h
libtme_display_la_LDFLAGS = -version-info 0:0:0
libtme_display_la_LIBADD = $(top_builddir)/generic/libtme-generic.la $(GTK_LIBS)

EXTRA_DIST = keymap-def.h

BUILT_SOURCES = keymap.h
CLEANFILES = keymap.h
keymap.h: keymap-def.h
	  cp $< $@

include $(top_srcdir)/modules

SUBDIRS = . @TME_DISPLAY_HOSTS@
DIST_SUBDIRS = gtk rfb sdl
