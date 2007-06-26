# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

TESTS = \
	test-stanza-build \
	run-xmpp-connection-test.sh

check_PROGRAMS = \
	test-xmpp-connection    \
	test-r-multicast-transport-io \
	test-stanza-build

check_SCRIPTS = run-xmpp-connection-test.sh

EXTRA_DIST = simplemeshtest.py mesh.py $(check_SCRIPTS)


test_xmpp_connection_SOURCES = \
    test-xmpp-connection.c     \
    test-transport.c           \
    test-transport.h

test_xmpp_connection_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

test_xmpp_connection_CFLAGS = \
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


test_stanza_build_SOURCES = \
    test-stanza-build.c

test_stanza_build_LDADD = \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

test_stanza_build_CFLAGS = \
    $(AM_CFLAGS)

if HAVE_CHECK
check_PROGRAMS += check-main
TESTS += check-main
check_main_SOURCES =                     \
    check-main.c                         \
    check-helpers.c                      \
    check-helpers.h                      \
    check-gibber.h                       \
    check-salut.h                        \
    check-gibber-xmpp-node.c             \
    check-gibber-xmpp-reader.c           \
    check-node-properties.c              \
    check-gibber-r-multicast-transport.c \
    test-transport.c                     \
    test-transport.h                     \
    check-gibber-xmpp-connection.c       \
    check-gibber-r-multicast-packet.c    \
    check-gibber-r-multicast-sender.c    \
    check-gibber-iq-helper.c

check_main_LDADD = \
    @CHECK_LIBS@ \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(top_builddir)/src/libsalut-convenience.la \
    $(AM_LDFLAGS)

check_main_CFLAGS = \
    @CHECK_CFLAGS@ \
    $(AM_CFLAGS)

if HAVE_LIBSASL2
  check_main_SOURCES += \
      check-gibber-sasl-auth.c   \
      test-sasl-auth-server.c    \
      test-sasl-auth-server.h

  check_main_LDADD += @LIBSASL2_LIBS@
  check_main_CFLAGS += @LIBSASL2_CFLAGS@
endif

endif

AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ @DBUS_CFLAGS@ \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
    -I $(top_srcdir)/src -I $(top_builddir)/src

AM_LDFLAGS = @GLIB_LIBS@

clean-local:
	-rm -rf outputs
	-rm -f sasl-test.db

SUBDIRS = inputs

$(check_SCRIPTS): always-run
	chmod +x $(srcdir)/$@

.PHONY: always-run
