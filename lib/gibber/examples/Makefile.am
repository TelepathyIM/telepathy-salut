noinst_PROGRAMS = \
	test_tcp

test_tcp_SOURCES = \
	test-tcp.c

test_tcp_LDADD = \
	$(top_builddir)/lib/gibber/libgibber.la \
	$(AM_LDFLAGS)

test_xmpp_connection_CFLAGS = \
	@LIBXML2_CFLAGS@          \
	$(AM_CFLAGS)

AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ \
	-I $(top_srcdir)/lib -I $(top_builddir)/lib \
	-I $(top_srcdir)/src -I $(top_builddir)/src

AM_LDFLAGS = @GLIB_LIBS@


