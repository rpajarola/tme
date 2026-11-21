## Process this file with automake to produce Makefile.in
# Makefile.am for The Machine Emulator host/eth/slirp:

AUTOMAKE_OPTIONS = 1.4 gnu

SUBDIRS = libslirp .

AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir) -I$(top_srcdir)/lib -I$(srcdir)/.. -I. -D_TME_IMPL -DLIBSLIRP_STATIC

pkglib_LTLIBRARIES = tme_host_slirp.la
tme_host_slirp_la_SOURCES = slirp-eth.c
tme_host_slirp_la_LDFLAGS = -module -version-info 0:0:0
tme_host_slirp_la_LIBADD = ../libtme-eth.la libslirp/libslirp.la

include $(top_srcdir)/modules.am
