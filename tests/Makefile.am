noinst_PROGRAMS = \
	test-xmpp-connection

test_xmpp_connection_SOURCES = \
    test-xmpp-connection.c     \
    test-transport.c           \
    test-transport.h

test_xmpp_connection_LDADD = \
    $(top_builddir)/src/libsalut-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) \
    @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ @AVAHI_CFLAGS@ \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
    -I $(top_srcdir)/src -I $(top_builddir)/src
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LIBXML2_LIBS@ @AVAHI_LIBS@

TESTS = $(noinst_PROGRAMS)

EXTRA_DIST = \
  run-test.sh
