## Process this file with automake to produce Makefile.in
# Makefile.am for The Machine Emulator machine/sun:

AUTOMAKE_OPTIONS = 1.4 gnu

AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir) -I$(top_srcdir)/lib -I. -D_TME_IMPL

lib_LTLIBRARIES = libtme-machine-sun.la
libtme_machine_sun_la_SOURCES = sun-mmu.c \
	sun-bwtwo.c \
	sun-cgtwo.c \
	sun-cgsix.c \
	sun-fb.c sun-fb.h \
	sun-si.c \
	sun-obie.c \
	sun-idprom.c sun-idprom.h \
	sun3-eeprom-default.c
libtme_machine_sun_la_LDFLAGS = -version-info 0:0:0
libtme_machine_sun_la_LIBADD = $(top_builddir)/generic/libtme-generic.la

bin_PROGRAMS = tme-sun-idprom tme-sun-eeprom
tme_sun_idprom_SOURCES = tme-sun-idprom.c sun-idprom.c sun-idprom.h
# per-program flags, so sun-idprom.c's object for this program isn't
# the libtool library's:
tme_sun_idprom_CFLAGS = $(AM_CFLAGS)
tme_sun_eeprom_SOURCES = tme-sun-eeprom.c

examplesdir = $(pkgdatadir)/examples
dist_examples_DATA = sun-keyboards.txt my-sun-macros.txt

# the default Sun-3 EEPROM contents, for creating a missing EEPROM
# file, come from tme-sun-eeprom's own defaults:
BUILT_SOURCES = sun3-eeprom-default.c
CLEANFILES = sun3-eeprom-default.c
sun3-eeprom-default.c: tme-sun-eeprom$(EXEEXT)
	./tme-sun-eeprom$(EXEEXT) --format-input=text --format-output=c < /dev/null > $@.tmp
	mv $@.tmp $@
