CLEANFILES=

include $(top_srcdir)/rules/check.mak

SUPPRESSIONS=valgrind.supp dlopen.supp

AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ @WOCKY_CFLAGS@ \
    @DBUS_CFLAGS@ \
    -I $(top_srcdir) -I $(top_builddir) \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib

AM_LDFLAGS = @GLIB_LIBS@ \
	$(top_builddir)/lib/gibber/libgibber.la

clean-local:
	-rm -rf outputs

SUBDIRS = inputs

EXTRA_DIST = \
	test-transport.h \
	test-transport.c

$(check_SCRIPTS): always-run
	chmod +x $(srcdir)/$@

.PHONY: always-run

# ------------------------------------------------------------------------------
# Test programs

# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

TESTS = \
	run-xmpp-connection-test.sh

noinst_PROGRAMS = \
	test-xmpp-connection    \
	test-r-multicast-transport-io

check_SCRIPTS = run-xmpp-connection-test.sh

EXTRA_DIST += \
	simplemeshtest.py mesh.py $(check_SCRIPTS)

test_xmpp_connection_SOURCES = \
    test-xmpp-connection.c     \
    test-transport.c           \
    test-transport.h

test_xmpp_connection_LDADD = \
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

# ------------------------------------------------------------------------------
# Checks

check_PROGRAMS = \
	check-gibber-xmpp-reader \
	check-gibber-r-multicast-causal-transport \
	check-gibber-xmpp-connection \
	check-gibber-r-multicast-packet \
	check-gibber-r-multicast-sender \
	check-gibber-iq-helper \
	check-gibber-listener \
	check-gibber-xmpp-connection-listener \
	check-gibber-xmpp-error \
	check-gibber-unix-transport

test: ${TEST_PROGS}
	gtester -k --verbose $(check_PROGRAMS)

# ------------------------------------------------------------------------------
# Code Style

# Coding style checks
check_c_sources = \
    $(test_xmpp_connection_SOURCES) \
    $(test_r_multicast_transport_io_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk

check-local: check-coding-style test
