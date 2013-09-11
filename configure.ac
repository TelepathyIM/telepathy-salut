AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set salut_nano_version to 0.

m4_define([salut_major_version], [0])
m4_define([salut_minor_version], [8])
m4_define([salut_micro_version], [999])
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
AS_CASE([$host],
  [*-*-*mingw*|*-*-*cygwin*],
    [platform=win32],
  [*],
    [platform=generic])

AC_MSG_RESULT($platform)

AM_CONDITIONAL([OS_WINDOWS], [test "$platform" = "win32"])

ifelse(salut_nano_version, 0,
[ # version x.y.z - "official release",
  # disable extra checks by default
  official_release=yes
  AC_ARG_ENABLE([fatal-warnings],
    [AC_HELP_STRING([--enable-fatal-warnings],
                    [make various warnings fatal])],
    [],
    [enable_fatal_warnings=no])
],
[ # version x.y.z.1 - development snapshot,
  # enable extra checks by default
  official_release=no
  AC_ARG_ENABLE([fatal-warnings],
    [AC_HELP_STRING([--disable-fatal-warnings],
                    [make various warnings non-fatal])],
    [],
    [enable_fatal_warnings=yes])
])

AC_SUBST([enable_fatal_warnings])

TP_COMPILER_WARNINGS([ERROR_CFLAGS], [test "x$enable_fatal_warnings" = xyes],
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

AC_ARG_ENABLE([debug],
      [AC_HELP_STRING([--disable-debug], [compile without debug code])],
      [], [enable_debug=yes])

AS_IF([test x$enable_debug = xyes],
  [AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])])

dnl Check for code generation tools
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
AS_IF([test -z "$XSLTPROC"],
  [AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])])

AM_PATH_PYTHON([2.5])

# Check for a python >= 2.5 with twisted to run python tests
AC_MSG_CHECKING([for Python with Avahi, Twisted and XMPP protocol support])
AS_IF([$PYTHON -c "import twisted.words.xish.domish, twisted.words.protocols.jabber, twisted.internet.reactor, avahi" >/dev/null 2>&1],
    [TEST_PYTHON="$PYTHON"],
    [TEST_PYTHON=false])
AC_MSG_RESULT([$TEST_PYTHON])
AC_SUBST(TEST_PYTHON)
AM_CONDITIONAL([WANT_TWISTED_TESTS], test false != "$TEST_PYTHON")

dnl olpc specific code switch
AC_ARG_ENABLE(olpc,
  AC_HELP_STRING([--enable-olpc],[compile with olpc specific code]),
    enable_olpc=$enableval, enable_olpc=no )

AS_IF([test x$enable_olpc = xyes],
  [AC_DEFINE(ENABLE_OLPC, [], [Enable olpc code])])
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
    [glib-2.0 >= 2.32.0, gobject-2.0 >= 2.32.0, gthread-2.0 >= 2.32.0, gio-2.0 >= 2.32.0])

dnl MIN_REQUIRED must stay to 2.30 because of GValueArray
AC_DEFINE([GLIB_VERSION_MIN_REQUIRED], [GLIB_VERSION_2_30], [Ignore post 2.32 deprecations])
AC_DEFINE([GLIB_VERSION_MAX_ALLOWED], [GLIB_VERSION_2_32], [Prevent post 2.32 APIs])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)


AS_IF([test "$platform" = "win32"],
[
PKG_CHECK_MODULES(GIO_WINDOWS,
    [gio-windows-2.0])
AC_SUBST(GIO_WINDOWS_CFLAGS)
AC_SUBST(GIO_WINDOWS_LIBS)
],
[
PKG_CHECK_MODULES(GIO_UNIX,
    [gio-unix-2.0])
AC_SUBST(GIO_UNIX_CFLAGS)
AC_SUBST(GIO_UNIX_LIBS)
])

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

AC_DEFINE([TP_SEAL_ENABLE], [], [Prevent to use sealed variables])
AC_DEFINE([TP_DISABLE_SINGLE_INCLUDE], [], [Disable single header include])
AC_DEFINE([TP_VERSION_MIN_REQUIRED], [TP_VERSION_0_20], [Ignore post 0.20 deprecations])
AC_DEFINE([TP_VERSION_MAX_ALLOWED], [TP_VERSION_0_20], [Prevent post 0.20 APIs])
PKG_CHECK_MODULES([TELEPATHY_GLIB], [telepathy-glib >= 0.20])

AC_SUBST(TELEPATHY_GLIB_CFLAGS)
AC_SUBST(TELEPATHY_GLIB_LIBS)

dnl check the backend to use
AS_IF([test "$platform" = "win32"],
[
AC_ARG_WITH(backend,
  AC_HELP_STRING([--with-backend=[bonjour/no]],
                 [Zeroconf backend to use]),
  [], [with_backend=bonjour])
],
[
AC_ARG_WITH(backend,
  AC_HELP_STRING([--with-backend=[avahi/bonjour/no]],
                 [Zeroconf backend to use]),
  [], [with_backend=avahi])
])

AM_CONDITIONAL(USE_BACKEND_AVAHI, [test "x$with_backend" = "xavahi"])
AM_CONDITIONAL(USE_BACKEND_BONJOUR, [test "x$with_backend" = "xbonjour"])
AM_CONDITIONAL(USE_BACKEND_DUMMY, [test "x$with_backend" = "xno"])

dnl Check for Avahi
AS_IF([test x$with_backend = xavahi],
[
  PKG_CHECK_MODULES(AVAHI, [avahi-gobject, avahi-client])
  AC_DEFINE(USE_BACKEND_AVAHI, [], [Use Avahi backend])
  AC_SUBST(AVAHI_CFLAGS)
  AC_SUBST(AVAHI_LIBS)
  AC_SUBST(USE_BACKEND_AVAHI)
  backend="Avahi"
])

dnl Check for Bonjour Client Library libdns_sd
AS_IF([test x$with_backend = xbonjour],
[
  PKG_CHECK_MODULES(BONJOUR, [libdns_sd])
  AC_DEFINE(USE_BACKEND_BONJOUR, [], [Use Bonjour backend])
  AC_SUBST(BONJOUR_CFLAGS)
  AC_SUBST(BONJOUR_LIBS)
  AC_SUBST(USE_BACKEND_BONJOUR)
  backend="Bonjour"
])

AS_IF([test x$with_backend = xno],
[
  AC_DEFINE(USE_BACKEND_DUMMY, [], [Use dummy backend])
  AC_SUBST(USE_BACKEND_DUMMY)
  backend="Dummy"
])

dnl Check for libsoup
PKG_CHECK_MODULES(LIBSOUP, [libsoup-2.4 >= 2.24])
AC_SUBST(LIBSOUP_CFLAGS)
AC_SUBST(LIBSOUP_LIBS)
AC_DEFINE([SOUP_VERSION_MIN_REQUIRED], [SOUP_VERSION_2_24], [Ignore post 2.24 deprecations])
AC_DEFINE([SOUP_VERSION_MAX_ALLOWED], [SOUP_VERSION_2_24], [Prevent post 2.24 APIs])

dnl check for libuuid
PKG_CHECK_MODULES([UUID], [uuid], [HAVE_UUID=yes], [HAVE_UUID=no])
AC_SUBST([UUID_CFLAGS])
AC_SUBST([UUID_LIBS])
AS_IF([test x"$HAVE_UUID" = xyes],
  [AC_DEFINE([HAVE_UUID], [1], [Define if libuuid is available])],
  [AC_MSG_WARN([libuuid not found, falling back to generating random IDs])])

AC_ARG_ENABLE(submodules,
  AS_HELP_STRING([--disable-submodules],
                 [Use system version of Wocky rather than a submodule]),
  build_submodules=$enableval,
  build_submodules=yes )

# These must contain "exec" for automake to work right (install-exec,
# not install-data).
#
# Private directory for Wocky and the salut-plugins library
AS_IF([test "x$pluginexeclibdir" = x],
  [pluginexeclibdir='${libdir}/telepathy/salut-0/lib'])
AC_ARG_VAR([pluginexeclibdir])
# The actual plugins
AS_IF([test "x$pluginexecdir" = x],
  [pluginexecdir='${libdir}/telepathy/salut-0/plugins'])
AC_ARG_VAR([pluginexecdir])

dnl plugins
AC_ARG_ENABLE(plugins,
  AC_HELP_STRING([--disable-plugins],
                 [disable plugin loader]),
    [enable_plugins=$enableval], [enable_plugins=yes])

AS_IF([test x$enable_plugins = xyes],
[
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
])

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
AS_IF([test "x${prefix}" = "xNONE"], [prefix=/usr/local])

# We tell Wocky to install its headers alongside gabble's so that an actual
# separate Wocky installation won't clash with them. This is a bit of a hack.
# AX_CONFIG_DIR doesn't make it very easy to pass extra arguments to the
# submodule's configure.

prev_ac_configure_args=$ac_configure_args
ac_configure_args="$ac_configure_args --with-installed-headers=${wocky_install_headers_dir} --enable-shared-suffix=${PACKAGE}-${VERSION} --libdir=${pluginexeclibdir}"

# meaningless for Salut's use
ac_configure_args="$ac_configure_args --disable-google-relay"

AS_IF([test "x$enable_fatal_warnings" = xyes],
  [ac_configure_args="$ac_configure_args --enable-coding-style-checks"],
  [ac_configure_args="$ac_configure_args --disable-coding-style-checks"])

AS_IF([test "x$tp_werror" = xyes && test "x$enable_fatal_warnings" = xyes],
  [ac_configure_args="$ac_configure_args --enable-Werror"],
  [ac_configure_args="$ac_configure_args --disable-Werror"])

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

if test false != "$TEST_PYTHON"; then
  tests_enabled=yes
else
  tests_enabled=no
fi

echo "
Configure summary:

        Compiler....................:  ${CC}
        Compiler Flags..............:  ${CFLAGS} ${ERROR_CFLAGS}
        Prefix......................:  ${prefix}
        Backend.....................:  ${backend}
        Enable debug................:  ${enable_debug}
        Python tests................:  ${tests_enabled}
        Plugins.....................:  ${enable_plugins}

Features:

        OLPC support................:  ${enable_olpc}

"
