# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	make -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

noinst_PROGRAMS = \
	test-xmpp-connection test-sasl-auth

test_xmpp_connection_SOURCES = \
    test-xmpp-connection.c     \
    test-transport.c           \
    test-transport.h

test_xmpp_connection_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

test_xmpp_connection_CFLAGS = \
    @LIBXML2_CFLAGS@          \
    $(AM_CFLAGS)

test_sasl_auth_SOURCES = \
    test-sasl-auth.c           \
    test-sasl-auth-server.c    \
    test-sasl-auth-server.h    \
    test-transport.c           \
    test-transport.h

test_sasl_auth_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

test_sasl_auth_CFLAGS = \
    @LIBXML2_CFLAGS@          \
    $(AM_CFLAGS)

AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
    -I $(top_srcdir)/src -I $(top_builddir)/src

AM_LDFLAGS = @GLIB_LIBS@

TESTS = test-sasl-auth run-test.sh

EXTRA_DIST = \
  run-test.sh

clean-local:
	-rm -rf outputs

SUBDIRS = inputs
