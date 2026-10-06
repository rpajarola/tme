## Process this file with automake to produce Makefile.in
# Makefile.am for The Machine Emulator host/eth/udp:

AUTOMAKE_OPTIONS = 1.4 gnu

AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir) -I$(top_srcdir)/lib -I$(srcdir)/.. -I. -D_TME_IMPL

pkglib_LTLIBRARIES = tme_host_udp.la
tme_host_udp_la_SOURCES = udp-eth.c
tme_host_udp_la_LDFLAGS = -module -version-info 0:0:0
tme_host_udp_la_LIBADD = ../libtme-eth.la

include $(top_srcdir)/modules
