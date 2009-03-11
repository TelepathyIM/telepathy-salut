CLEANFILES=

include $(top_srcdir)/rules/check.mak

SUPPRESSIONS=valgrind.supp dlopen.supp

# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

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

if HAVE_CHECK
check_PROGRAMS = check-main
TESTS = check-main
check_main_SOURCES =                             \
    check-main.c                                 \
    check-helpers.c                              \
    check-helpers.h                              \
    check-node-properties.c                      \
    check-salut.h

check_main_LDADD = \
    @CHECK_LIBS@ \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(top_builddir)/src/libsalut-convenience.la \
    $(top_builddir)/src/libsalut-backend-avahi.la \
    $(AM_LDFLAGS)

check_main_CFLAGS = \
    @CHECK_CFLAGS@ \
    $(AM_CFLAGS)

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

SUBDIRS = twisted

$(check_SCRIPTS): always-run
	chmod +x $(srcdir)/$@

.PHONY: always-run
