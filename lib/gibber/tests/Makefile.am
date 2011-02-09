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
    check-gibber-xmpp-node.c                     \
    check-gibber-xmpp-reader.c                   \
    check-gibber-r-multicast-causal-transport.c  \
    test-transport.c                             \
    test-transport.h                             \
    check-gibber-xmpp-connection.c               \
    check-gibber-r-multicast-packet.c            \
    check-gibber-r-multicast-sender.c            \
    check-wocky-stanza.c                   \
    check-gibber-iq-helper.c                     \
    check-gibber-listener.c                      \
    check-gibber-xmpp-connection-listener.c      \
    check-gibber-xmpp-error.c                    \
    check-gibber-unix-transport.c

check_main_LDADD = \
    @CHECK_LIBS@ \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(AM_LDFLAGS)

check_main_CFLAGS = \
    @CHECK_CFLAGS@ \
    $(AM_CFLAGS)

endif

AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ @WOCKY_CFLAGS@ \
    @DBUS_CFLAGS@ \
    -I $(top_srcdir) -I $(top_builddir) \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib

AM_LDFLAGS = @GLIB_LIBS@

# Coding style checks
check_c_sources = \
    $(test_xmpp_connection_SOURCES) \
    $(test_r_multicast_transport_io_SOURCES)

if HAVE_CHECK
    check_c_sources += $(check_main_SOURCES)
endif

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

clean-local:
	-rm -rf outputs

SUBDIRS = inputs

$(check_SCRIPTS): always-run
	chmod +x $(srcdir)/$@

.PHONY: always-run
