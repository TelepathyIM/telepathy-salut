# All examples use SSL, so don't build anything if it's unavailable
if HAVE_LIBSSL

# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	make -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

noinst_PROGRAMS = \
	test_tcp test_ssl

test_tcp_SOURCES = \
	test-tcp.c

test_tcp_LDADD = \
	$(top_builddir)/lib/gibber/libgibber.la \
	$(AM_LDFLAGS)

test_ssl_SOURCES = \
	test-ssl.c

test_ssl_LDADD = \
	$(top_builddir)/lib/gibber/libgibber.la \
	$(AM_LDFLAGS)

test_xmpp_connection_CFLAGS = \
	@LIBXML2_CFLAGS@          \
	$(AM_CFLAGS)

AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ \
	-I $(top_srcdir)/lib -I $(top_builddir)/lib \
	-I $(top_srcdir)/src -I $(top_builddir)/src

AM_LDFLAGS = @GLIB_LIBS@

endif
