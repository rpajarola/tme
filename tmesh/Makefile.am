## Process this file with automake to produce Makefile.in
# Makefile.am for The Machine Emulator tmesh/:

AUTOMAKE_OPTIONS = 1.4 gnu

AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir) -I$(top_srcdir)/lib -I. -D_TME_IMPL

noinst_LTLIBRARIES = libtmesh.la
libtmesh_la_SOURCES = \
	tmesh-impl.h \
	tmesh-input.y \
	tmesh-util.c \
	tmesh-cmds.c \
	tmesh-threads.c
libtmesh_la_LIBADD = $(top_builddir)/libtme/libtme.la

bin_PROGRAMS = tmesh
tmesh_LDADD = libtmesh.la $(LDADD)
tmesh_LDFLAGS = -dlpreopen force -dlopen self
include $(top_srcdir)/tme-preopen.txt

# with builtin modules, tmesh is linked with every module, and with
# a copy of their index, so it needs no files to run.  linking
# statically makes libtool preload the -dlopen modules above:
if TME_BUILTIN_MODULES
AM_CPPFLAGS += -DTME_BUILTIN_MODULES
tmesh_LDFLAGS += -static
BUILT_SOURCES = tmesh-plugins.h
CLEANFILES = tmesh-plugins.h
tmesh-plugins.h: $(top_srcdir)/tme/tme-plugins.txt
	{ echo 'static const char _tmesh_plugins[] ='; \
	  sed -e 's/.*/  "&\\n"/' $(top_srcdir)/tme/tme-plugins.txt; \
	  echo '  ;'; } > $@
endif

install-exec-hook:
	$(SETCAP) $(DESTDIR)$(bindir)/$(TGTPFX)tmesh$(EXEEXT)
