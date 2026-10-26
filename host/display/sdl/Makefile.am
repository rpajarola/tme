## Process this file with automake to produce Makefile.in
# Makefile.am for The Machine Emulator host/rfb:

AUTOMAKE_OPTIONS = 1.4 gnu

AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir) -I$(top_srcdir)/lib -I$(srcdir)/.. -I. -D_TME_IMPL $(RFB_CFLAGS) $(SDL_CFLAGS)

if HAVE_RFB_LIBS
bin_PROGRAMS = SDLvncviewer
LDADD = ../libtme-display.la $(RFB_LIBS) $(SDL_LIBS)
endif

pkglib_LTLIBRARIES = tme_host_sdl.la
tme_host_sdl_la_SOURCES = sdl-display.c sdl-display.h sdl-icon.h sdl-menu.c sdl-dialog.c sdl-files.c sdl-font.h
tme_host_sdl_la_LDFLAGS = -module -version-info 0:0:0
tme_host_sdl_la_LIBADD = ../libtme-display.la $(SDL_LIBS)

EXTRA_DIST = sdl-font.sh

include $(top_srcdir)/modules.am
