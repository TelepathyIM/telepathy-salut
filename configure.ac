AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set salut_nano_version to 0.

m4_define([salut_major_version], [0])
m4_define([salut_minor_version], [7])
m4_define([salut_micro_version], [2])
m4_define([salut_nano_version], [1])

# Some magic
m4_define([salut_base_version],
          [salut_major_version.salut_minor_version.salut_micro_version])
m4_define([salut_version],
          [m4_if(salut_nano_version, 0, [salut_base_version], [salut_base_version].[salut_nano_version])])dnl

AC_INIT([Telepathy Salut], [salut_version],
[https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=telepathy-salut])

AC_CONFIG_MACRO_DIR([m4])
AC_LANG([C])
AM_INIT_AUTOMAKE([1.9 -Wno-portability tar-ustar])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])
AM_PROG_LIBTOOL
AM_CONFIG_HEADER(config.h)

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_AS
AM_PROG_MKDIR_P

dnl add common command line switches
SALUT_ARG_DEBUG
SALUT_ARG_VALGRIND
SALUT_ARG_COVERAGE

dnl Detect Windows Verion
AC_MSG_CHECKING([for operating system])
case "$host" in
  *-*-*mingw*|*-*-*cygwin*)
    platform=win32
    ;;
  *)
    platform=generic
    ;;
esac

AC_MSG_RESULT($platform)

AM_CONDITIONAL([OS_WINDOWS], [test "$platform" = "win32"])

dnl decide error flags
ifelse(salut_nano_version, 0,
    [ official_release=yes ],
    [ official_release=no ])

TP_COMPILER_WARNINGS([ERROR_CFLAGS], [test "x$official_release" = xno],
  [all \
   extra \
   declaration-after-statement \
   shadow \
   strict-prototypes \
   missing-prototypes \
   sign-compare \
   nested-externs \
   pointer-arith \
   format-security \
   init-self],
  [missing-field-initializers \
   unused-parameter])
AC_SUBST([ERROR_CFLAGS])

ifelse(salut_nano_version, 0,
    [ # Salut is version x.y.z - disable coding style checks by default
AC_ARG_ENABLE(coding-style-checks,
  AC_HELP_STRING([--enable-coding-style-checks],
                 [check coding style using grep]),
    [ENABLE_CODING_STYLE_CHECKS=$enableval], [ENABLE_CODING_STYLE_CHECKS=no] )
    ],
    [ # Salut is version x.y.z.1 - enable coding style checks by default
AC_ARG_ENABLE(coding-style-checks,
  AC_HELP_STRING([--disable-coding-style-checks],
                 [do not check coding style using grep]),
    [ENABLE_CODING_STYLE_CHECKS=$enableval], [ENABLE_CODING_STYLE_CHECKS=yes])
    ])

if test x$enable_debug = xyes; then
  AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
fi

AC_SUBST([ENABLE_CODING_STYLE_CHECKS])

dnl Check for code generation tools
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi

AM_PATH_PYTHON([2.5])

# Check for a python >= 2.5 with twisted to run python tests
AC_MSG_CHECKING([for Python with Avahi, Twisted and XMPP protocol support])
if $PYTHON -c "import twisted.words.xish.domish, twisted.words.protocols.jabber, twisted.internet.reactor, avahi" >/dev/null 2>&1; then
    TEST_PYTHON="$PYTHON"
else
    TEST_PYTHON=false
fi
AC_MSG_RESULT([$TEST_PYTHON])
AC_SUBST(TEST_PYTHON)
AM_CONDITIONAL([WANT_TWISTED_TESTS], test false != "$TEST_PYTHON")

dnl enable avahi tests
AC_ARG_ENABLE(avahi-tests,
  AC_HELP_STRING([ --enable-avahi-tests],
    [Enable tests that use the system avahi to hit the network ]),
    enable_avahi_tests=$enableval, enable_avahi_tests=yes)

if test x$enable_avahi_tests = xyes; then
  if test false = "$TEST_PYTHON"; then
    AC_MSG_ERROR(
      [Python with twisted support is needed for avahi twisted tests])
  fi
fi
AM_CONDITIONAL(WANT_TWISTED_AVAHI_TESTS, test "x$enable_avahi_tests" = "xyes")

dnl olpc specific code switch
AC_ARG_ENABLE(olpc,
  AC_HELP_STRING([--enable-olpc],[compile with olpc specific code]),
    enable_olpc=$enableval, enable_olpc=no )

if test x$enable_olpc = xyes; then
  AC_DEFINE(ENABLE_OLPC, [], [Enable olpc code])
fi

AM_CONDITIONAL(ENABLE_OLPC, test "x$enable_olpc" = "xyes")

AC_SUBST(ENABLE_OLPC)

AC_HEADER_STDC([])
AC_C_INLINE

AC_CHECK_HEADERS_ONCE([
    arpa/inet.h
    arpa/nameser.h
    fcntl.h
    ifaddrs.h
    netdb.h
    netinet/in.h
    sys/ioctl.h
    sys/un.h
    unistd.h
    ])

# on Darwin, these headers are interdependent, according to autoconf.info
AC_CHECK_HEADERS([sys/socket.h], [], [],
[
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
])

# Autoconf has a handy macro for this, since it tends to have dependencies
AC_HEADER_RESOLV

dnl GTK docs
GTK_DOC_CHECK

dnl Check for Glib
PKG_CHECK_MODULES(GLIB,
    [glib-2.0 >= 2.24, gobject-2.0 >= 2.16, gthread-2.0 >= 2.4, gio-2.0])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)


if test "$platform" = "win32"; then
PKG_CHECK_MODULES(GIO_WINDOWS,
    [gio-windows-2.0])
AC_SUBST(GIO_WINDOWS_CFLAGS)
AC_SUBST(GIO_WINDOWS_LIBS)
else
PKG_CHECK_MODULES(GIO_UNIX,
    [gio-unix-2.0])
AC_SUBST(GIO_UNIX_CFLAGS)
AC_SUBST(GIO_UNIX_LIBS)
fi

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

dnl Check for D-Bus
PKG_CHECK_MODULES(DBUS, [dbus-1 >= 1.1.0, dbus-glib-1 >= 0.61])

AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

dnl Check for libxml2
PKG_CHECK_MODULES(LIBXML2, [libxml-2.0])

AC_SUBST(LIBXML2_CFLAGS)
AC_SUBST(LIBXML2_LIBS)

dnl Check for telepathy-glib
PKG_CHECK_MODULES(TELEPATHY_GLIB, [telepathy-glib >= 0.17.1])

AC_SUBST(TELEPATHY_GLIB_CFLAGS)
AC_SUBST(TELEPATHY_GLIB_LIBS)

dnl check the backend to use
if test "$platform" = "win32"; then
AC_ARG_WITH(backend,
  AC_HELP_STRING([--with-backend=[bonjour/no]],
                 [Zeroconf backend to use]),
  [], [with_backend=bonjour])
else
AC_ARG_WITH(backend,
  AC_HELP_STRING([--with-backend=[avahi/bonjour/no]],
                 [Zeroconf backend to use]),
  [], [with_backend=avahi])
fi

AM_CONDITIONAL(USE_BACKEND_AVAHI, [test "x$with_backend" = "xavahi"])
AM_CONDITIONAL(USE_BACKEND_BONJOUR, [test "x$with_backend" = "xbonjour"])
AM_CONDITIONAL(USE_BACKEND_DUMMY, [test "x$with_backend" = "xno"])

dnl Check for Avahi
if test x$with_backend = xavahi; then
  PKG_CHECK_MODULES(AVAHI, [avahi-gobject, avahi-client])
  AC_DEFINE(USE_BACKEND_AVAHI, [], [Use Avahi backend])
  AC_SUBST(AVAHI_CFLAGS)
  AC_SUBST(AVAHI_LIBS)
  AC_SUBST(USE_BACKEND_AVAHI)
fi

dnl Check for Bonjour Client Library libdns_sd
if test x$with_backend = xbonjour; then
  PKG_CHECK_MODULES(BONJOUR, [libdns_sd])
  AC_DEFINE(USE_BACKEND_BONJOUR, [], [Use Bonjour backend])
  AC_SUBST(BONJOUR_CFLAGS)
  AC_SUBST(BONJOUR_LIBS)
  AC_SUBST(USE_BACKEND_BONJOUR)
fi

if test x$with_backend = xno; then
  AC_DEFINE(USE_BACKEND_DUMMY, [], [Use dummy backend])
  AC_SUBST(USE_BACKEND_DUMMY)
fi

dnl Check for libsoup
PKG_CHECK_MODULES(LIBSOUP, [libsoup-2.4])
AC_SUBST(LIBSOUP_CFLAGS)
AC_SUBST(LIBSOUP_LIBS)

dnl check for libuuid
PKG_CHECK_MODULES([UUID], [uuid], [HAVE_UUID=yes], [HAVE_UUID=no])
AC_SUBST([UUID_CFLAGS])
AC_SUBST([UUID_LIBS])
if test x"$HAVE_UUID" = xyes; then
  AC_DEFINE([HAVE_UUID], [1], [Define if libuuid is available])
else
  AC_MSG_WARN([libuuid not found, falling back to generating random IDs])
fi

AC_ARG_ENABLE(submodules,
  AS_HELP_STRING([--disable-submodules],
                 [Use system version of Wocky rather than a submodule]),
  build_submodules=$enableval,
  build_submodules=yes )

# These must contain "exec" for automake to work right (install-exec,
# not install-data).
#
# Private directory for Wocky and the salut-plugins library
if test "x$pluginexeclibdir" = x; then
  pluginexeclibdir='${libdir}/telepathy/salut-0/lib'
fi
AC_ARG_VAR([pluginexeclibdir])
# The actual plugins
if test "x$pluginexecdir" = x; then
  pluginexecdir='${libdir}/telepathy/salut-0/plugins'
fi
AC_ARG_VAR([pluginexecdir])

dnl plugins
AC_ARG_ENABLE(plugins,
  AC_HELP_STRING([--disable-plugins],
                 [disable plugin loader]),
    [enable_plugins=$enableval], [enable_plugins=yes])

if test x$enable_plugins = xyes; then
  AC_DEFINE(ENABLE_PLUGINS, [], [Enable plugins])
  PKG_CHECK_MODULES(GMODULE, [gmodule-2.0])

  AC_ARG_ENABLE(plugin-api,
    AC_HELP_STRING([--enable-plugin-api],
                   [install headers for third-party plugins (experimental)]),
    [
      enable_plugin_api=$enableval
      wocky_install_headers_dir="${includedir}/telepathy-salut-0"
    ],
    [enable_plugin_api=no])
fi

AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

AM_CONDITIONAL(ENABLE_PLUGINS, test x$enable_plugins = xyes)
AC_SUBST(ENABLE_PLUGINS)

AM_CONDITIONAL(ENABLE_PLUGIN_API, test x$enable_plugin_api = xyes)

# We have to run Wocky's configure *before* looking for it with
# PKG_CHECK_MODULES so wocky-uninstalled.pc has been generated

# If you don't specify --prefix, it starts off as NONE. Autoconf
# would normally do this defaulting for us later, but that's too
# late to help Wocky.
if test "x${prefix}" = "xNONE"; then
  prefix=/usr/local
fi

# We tell Wocky to install its headers alongside gabble's so that an actual
# separate Wocky installation won't clash with them. This is a bit of a hack.
# AX_CONFIG_DIR doesn't make it very easy to pass extra arguments to the
# submodule's configure.

prev_ac_configure_args=$ac_configure_args
ac_configure_args="$ac_configure_args --with-installed-headers=${wocky_install_headers_dir} --enable-shared-suffix=${PACKAGE}-${VERSION} --libdir=${pluginexeclibdir}"

if test "x$ENABLE_CODING_STYLE_CHECKS" = xyes ; then
  ac_configure_args="$ac_configure_args --enable-coding-style-checks"
else
  ac_configure_args="$ac_configure_args --disable-coding-style-checks"
fi

if test "x$tp_werror" = xyes && test "x$official_release" = xno; then
  ac_configure_args="$ac_configure_args --enable-Werror"
else
  ac_configure_args="$ac_configure_args --disable-Werror"
fi

prev_top_build_prefix=$ac_top_build_prefix
AX_CONFIG_DIR([lib/ext/wocky])
ac_top_build_prefix=$prev_top_build_prefix

ac_configure_args=$prev_ac_configure_args

PKG_CONFIG_PATH="${PKG_CONFIG_PATH:+"$PKG_CONFIG_PATH:"}${ac_abs_top_builddir}/lib/ext/wocky/wocky"
export PKG_CONFIG_PATH
PKG_CHECK_MODULES([WOCKY], [wocky >= 0.0.0])
AC_SUBST([WOCKY_CFLAGS])
AC_SUBST([WOCKY_LIBS])

AC_SUBST(PACKAGE_STRING)

AC_OUTPUT( Makefile                       \
           docs/Makefile                  \
           lib/Makefile                   \
           lib/ext/Makefile               \
           lib/gibber/Makefile            \
           lib/gibber/tests/Makefile      \
           src/Makefile                   \
           m4/Makefile                    \
           data/Makefile                  \
           extensions/Makefile            \
           tools/Makefile                 \
           tests/Makefile                 \
           tests/twisted/Makefile         \
           tests/twisted/tools/Makefile   \
           plugins/Makefile               \
           salut/Makefile                 \
           salut/telepathy-salut-uninstalled.pc \
           salut/telepathy-salut.pc
)
