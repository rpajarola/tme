## Process this file with automake to produce Makefile.in
# Makefile.am for The Machine Emulator host/eth/slirp/libslirp:

# this is libslirp, the user-mode TCP/IP stack, from
# https://gitlab.freedesktop.org/slirp/libslirp.  see README.tme.  its
# glib.h and glib.c replace GLib, so the sources here include them
# instead of GLib's:

AUTOMAKE_OPTIONS = 1.4 gnu

AM_CPPFLAGS = -I$(srcdir) -DBUILDING_LIBSLIRP -DLIBSLIRP_STATIC

noinst_LTLIBRARIES = libslirp.la
libslirp_la_SOURCES = \
	glib.c arp_table.c bootp.c cksum.c dhcpv6.c dnssearch.c if.c \
	ip6_icmp.c ip6_input.c ip6_output.c ip_icmp.c ip_input.c \
	ip_output.c mbuf.c misc.c ncsi.c ndp_table.c sbuf.c slirp.c \
	socket.c state.c stream.c tcp_input.c tcp_output.c tcp_subr.c \
	tcp_timer.c tftp.c udp6.c udp.c util.c version.c vmstate.c \
	bootp.h debug.h dhcpv6.h glib.h if.h ip6.h ip6_icmp.h ip.h \
	ip_icmp.h libslirp.h libslirp-version.h main.h mbuf.h misc.h \
	ncsi-pkt.h sbuf.h slirp.h socket.h stream.h tcp.h tcpip.h \
	tcp_timer.h tcp_var.h tftp.h udp.h util.h vmstate.h winver.h
libslirp_la_LIBADD = $(SLIRP_LIBS)

EXTRA_DIST = COPYRIGHT LICENSE README.tme
