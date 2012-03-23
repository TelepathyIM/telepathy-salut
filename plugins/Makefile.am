# testing-only plugins
noinst_LTLIBRARIES = \
	test.la

installable_plugins =
	$(NULL)

if ENABLE_PLUGINS
pluginexec_LTLIBRARIES = $(installable_plugins)
else
# we still compile the plugin (just to make sure it compiles!) but we don't
# install it
noinst_LTLIBRARIES += $(installable_plugins)
endif

AM_LDFLAGS = -module -avoid-version -shared

test_la_SOURCES = \
    test.c \
    test.h

# because test.la is not installed, libtool will want to compile it as static
# despite -shared (a convenience library), unless we also use -rpath
test_la_LDFLAGS = $(AM_LDFLAGS) -rpath $(pluginexecdir)

AM_CFLAGS = $(ERROR_CFLAGS) \
    -I $(top_srcdir) -I $(top_builddir) \
    @GLIB_CFLAGS@ @TELEPATHY_GLIB_CFLAGS@ @WOCKY_CFLAGS@ \
    -I $(top_srcdir)/salut -I $(top_builddir)/salut \
    -I $(top_srcdir)/plugins
