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

TESTS =

noinst_PROGRAMS = \
	test-r-multicast-transport-io

check_SCRIPTS =

EXTRA_DIST += \
	simplemeshtest.py mesh.py $(check_SCRIPTS)

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
	check-gibber-r-multicast-causal-transport \
	check-gibber-r-multicast-packet \
	check-gibber-r-multicast-sender \
	check-gibber-listener \
	check-gibber-unix-transport

test: ${TEST_PROGS}
	gtester -k --verbose $(check_PROGRAMS)

# ------------------------------------------------------------------------------
# Code Style

# Coding style checks
check_c_sources = \
    $(test_r_multicast_transport_io_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk

check-local: check-coding-style test
