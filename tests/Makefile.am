# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

noinst_PROGRAMS = \
	test-xmpp-connection    \
	test-r-multicast-packet \
	test-r-multicast-sender \
	test-r-multicast-transport-io


test_xmpp_connection_SOURCES = \
    test-xmpp-connection.c     \
    test-transport.c           \
    test-transport.h

test_xmpp_connection_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

test_xmpp_connection_CFLAGS = \
    $(AM_CFLAGS)

test_r_multicast_packet_SOURCES = \
    test-r-multicast-packet.c

test_r_multicast_packet_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

test_r_multicast_packet_CFLAGS = \
    $(AM_CFLAGS)


test_r_multicast_sender_SOURCES = \
    test-r-multicast-sender.c

test_r_multicast_sender_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

test_r_multicast_sender_CFLAGS = \
    $(AM_CFLAGS)

test_r_multicast_transport_io_SOURCES = \
    test-r-multicast-transport-io.c     \
    test-transport.c           \
    test-transport.h

test_r_multicast_transport_io_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

test_r_multicast_transport_io_CFLAGS = \
    $(AM_CFLAGS)


if HAVE_LIBSASL2
noinst_PROGRAMS += test-sasl-auth

test_sasl_auth_SOURCES = \
    test-sasl-auth.c           \
    test-sasl-auth-server.c    \
    test-sasl-auth-server.h    \
    test-transport.c           \
    test-transport.h

test_sasl_auth_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    @LIBSASL2_LIBS@ \
    $(AM_LDFLAGS)

test_sasl_auth_CFLAGS = \
    @LIBSASL2_CFLAGS@     \
    $(AM_CFLAGS)

endif

AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
    -I $(top_srcdir)/src -I $(top_builddir)/src

AM_LDFLAGS = @GLIB_LIBS@

TESTS = run-xmpp-connection-test.sh

if HAVE_LIBSASL2
  TESTS +=  test-sasl-auth
endif

EXTRA_DIST = \
  run-xmpp-connection-test.sh

clean-local:
	-rm -rf outputs
	-rm -f sasl-test.db

SUBDIRS = inputs
