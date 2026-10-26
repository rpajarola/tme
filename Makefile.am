## Process this file with automake to produce Makefile.in
# Makefile.am for The Machine Emulator:

AUTOMAKE_OPTIONS = 1.4 gnu
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = . tme lib libltdl libtme generic ic machine host bus serial scsi tmesh tools
EXTRA_DIST = modules.am edist $(icon_files)
pkgdata_DATA = tmeconfig.h tmememory.h

# The desktop entry and the icons of the icon theme, made by
# packaging/icon/make-icons.sh:
icon_sizes = 16 24 32 48 64 128 256
icon_files = packaging/icon/make-icons.sh packaging/icon/tme-icon.svg \
	packaging/icon/tme.desktop packaging/icon/png/tme-16.png \
	packaging/icon/png/tme-24.png packaging/icon/png/tme-32.png \
	packaging/icon/png/tme-48.png packaging/icon/png/tme-64.png \
	packaging/icon/png/tme-128.png packaging/icon/png/tme-256.png

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(datadir)/applications
	$(INSTALL_DATA) $(srcdir)/packaging/icon/tme.desktop $(DESTDIR)$(datadir)/applications/tme.desktop
	for n in $(icon_sizes); do \
	  $(MKDIR_P) $(DESTDIR)$(datadir)/icons/hicolor/$${n}x$${n}/apps; \
	  $(INSTALL_DATA) $(srcdir)/packaging/icon/png/tme-$$n.png \
	    $(DESTDIR)$(datadir)/icons/hicolor/$${n}x$${n}/apps/tme.png; \
	done
	$(MKDIR_P) $(DESTDIR)$(datadir)/icons/hicolor/scalable/apps
	$(INSTALL_DATA) $(srcdir)/packaging/icon/tme-icon.svg \
	  $(DESTDIR)$(datadir)/icons/hicolor/scalable/apps/tme.svg

uninstall-local:
	rm -f $(DESTDIR)$(datadir)/applications/tme.desktop
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/*/apps/tme.png
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/scalable/apps/tme.svg

# target to hook into the distribution:
#
dist-hook:
	rm -f ${distdir}/libtme/recode-host.c
	find $(distdir)/edist -name CVS -a -exec rm -r {} ';' -a -prune

# The target to update the list of modules to preopen on platforms that don't support dlopen.
# This will update this list to be automatically included by automake
# so that future builds will track all module dependencies.
# If new modules are added, "make modules" must be run to update this list in the source.
modules-local:
	rm -f ${top_srcdir}/tme/tme-plugins.txt
	printf 'LDADD = @TME_HOST''_PREOPEN@' > $(top_srcdir)/tme-preopen.txt
