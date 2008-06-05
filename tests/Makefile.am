CLEANFILES=

include $(top_srcdir)/rules/check.mak

SUPPRESSIONS=valgrind.supp dlopen.supp

# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

TESTS = \
	run-xmpp-connection-test.sh

check_PROGRAMS = \
	test-xmpp-connection    \
	test-r-multicast-transport-io

check_SCRIPTS = run-xmpp-connection-test.sh

EXTRA_DIST = simplemeshtest.py mesh.py $(check_SCRIPTS)

noinst_PROGRAMS = \
        telepathy-salut-debug

telepathy_salut_debug_SOURCES = \
    salut-debug.c

telepathy_salut_debug_LDADD = \
    $(top_builddir)/src/libsalut-convenience.la \
    $(top_builddir)/src/libsalut-backend-dummy.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(top_builddir)/extensions/libsalut-extensions.la \
    -ltelepathy-glib

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


if HAVE_CHECK
check_PROGRAMS += check-main
TESTS += check-main
check_main_SOURCES =                             \
    check-main.c                                 \
    check-helpers.c                              \
    check-helpers.h                              \
    check-gibber.h                               \
    check-salut.h                                \
    check-gibber-xmpp-node.c                     \
    check-gibber-xmpp-reader.c                   \
    check-node-properties.c                      \
    check-gibber-r-multicast-causal-transport.c  \
    check-gibber-resolver.c                      \
    test-resolver.c                              \
    test-resolver.h                              \
    test-transport.c                             \
    test-transport.h                             \
    check-gibber-xmpp-connection.c               \
    check-gibber-r-multicast-packet.c            \
    check-gibber-r-multicast-sender.c            \
    check-gibber-xmpp-stanza.c                   \
    check-gibber-iq-helper.c                     \
    check-gibber-xmpp-connection-listener.c \
    check-gibber-xmpp-error.c

check_main_LDADD = \
    @CHECK_LIBS@ \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(top_builddir)/src/libsalut-convenience.la \
    $(top_builddir)/src/libsalut-backend-avahi.la \
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
    @TELEPATHY_GLIB_CFLAGS@ \
    -I $(top_srcdir) -I $(top_builddir) \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
    -I $(top_srcdir)/src -I $(top_builddir)/src

AM_LDFLAGS = @GLIB_LIBS@ @TELEPATHY_GLIB_LIBS@

# Coding style checks
check_c_sources = \
    $(telepathy_salut_debug_SOURCES) \
    $(test_xmpp_connection_SOURCES) \
    $(test_r_multicast_transport_io_SOURCES)

if HAVE_CHECK
    check_c_sources += $(check_main_SOURCES)
endif

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

clean-local:
	-rm -rf outputs
	-rm -f sasl-test.db

SUBDIRS = inputs

$(check_SCRIPTS): always-run
	chmod +x $(srcdir)/$@

.PHONY: always-run
