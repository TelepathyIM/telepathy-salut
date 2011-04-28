CLEANFILES=

include $(top_srcdir)/rules/check.mak

SUPPRESSIONS=valgrind.supp dlopen.supp

SUBDIRS = twisted

.PHONY: always-run test test-report

# ------------------------------------------------------------------------------
# telepathy-salut-debug

noinst_PROGRAMS = \
        telepathy-salut-debug

telepathy_salut_debug_SOURCES = \
    debug.c

telepathy_salut_debug_LDADD = \
    $(top_builddir)/src/libsalut-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(top_builddir)/extensions/libsalut-extensions.la \
    -ltelepathy-glib

# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

# ------------------------------------------------------------------------------
# TESTS

check_PROGRAMS = check-node-properties

AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ @WOCKY_CFLAGS@ \
    @DBUS_CFLAGS@ @TELEPATHY_GLIB_CFLAGS@ \
    -I $(top_srcdir) -I $(top_builddir) \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
    -I $(top_srcdir)/src -I $(top_builddir)/src

AM_LDFLAGS = \
    @GLIB_LIBS@ @TELEPATHY_GLIB_LIBS@ @LIBSOUP_LIBS@ @WOCKY_LIBS@ @UUID_LIBS@

check_node_properties_LDADD = \
    $(top_builddir)/src/libsalut-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(top_builddir)/extensions/libsalut-extensions.la

test: ${TEST_PROGS}
	gtester -k --verbose $(check_PROGRAMS)

# ------------------------------------------------------------------------------
# CODING STYLE

# Coding style checks
check_c_sources = \
    $(telepathy_salut_debug_SOURCES) \
    $(test_xmpp_connection_SOURCES) \
    $(test_r_multicast_transport_io_SOURCES) \
    $(check_main_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk

check-local: check-coding-style test
