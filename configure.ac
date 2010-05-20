AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set salut_nano_version to 0.

m4_define([salut_major_version], [0])
m4_define([salut_minor_version], [3])
m4_define([salut_micro_version], [12])
m4_define([salut_nano_version], [1])

# Some magic
m4_define([salut_base_version],
          [salut_major_version.salut_minor_version.salut_micro_version])
m4_define([salut_version],
          [m4_if(salut_nano_version, 0, [salut_base_version], [salut_base_version].[salut_nano_version])])dnl

AC_INIT([Telepathy Salut], [salut_version],
[https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=telepathy-salut])

AC_CONFIG_MACRO_DIR([m4])

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
                 [don't check coding style using grep]),
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
DBUS_BINDING_TOOL=
AC_CHECK_PROGS([DBUS_BINDING_TOOL], [dbus-binding-tool])
if test -z "$DBUS_BINDING_TOOL"; then
  AC_MSG_ERROR([dbus-binding-tool (from dbus-glib) is required])
fi

AM_PATH_PYTHON([2.5])

# Check for a python >= 2.5 with twisted to run python tests
AC_MSG_CHECKING([for Python with Twisted and XMPP protocol support])
if $PYTHON -c "import twisted.words.xish.domish, twisted.words.protocols.jabber, twisted.internet.reactor" >/dev/null 2>&1; then
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
    [Enable tests that use the system avahi ]),
    enable_avahi_tests=$enableval, enable_avahi_tests=no)

if test x$enable_avahi_tests = xyes; then
  if test false == "$TEST_PYTHON"; then
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
AC_CHECK_HEADERS([net/if.h], [], [],
[
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
])

# Autoconf has a handy macro for this, since it tends to have dependencies
AC_HEADER_RESOLV

dnl Check endianness (Needed for the sha1 implementation)
AC_C_BIGENDIAN

dnl GTK docs
GTK_DOC_CHECK

dnl Check for Glib
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.16, gobject-2.0 >= 2.16, gthread-2.0 >= 2.4])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

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
PKG_CHECK_MODULES(TELEPATHY_GLIB, [telepathy-glib >= 0.7.36])

AC_SUBST(TELEPATHY_GLIB_CFLAGS)
AC_SUBST(TELEPATHY_GLIB_LIBS)

dnl Check for OpenSSL
AC_ARG_ENABLE(ssl,
  AC_HELP_STRING([--enable-ssl],[enable gibber ssl transport [[default=no]]]),
    enable_ssl=$enableval, enable_ssl=no )

if test x$enable_ssl = xyes; then
  PKG_CHECK_MODULES(LIBSSL, [openssl], 
              [ HAVE_LIBSSL="yes" ],
              [ AC_MSG_ERROR(openssl not found) ] )
else
  AC_MSG_NOTICE([openssl support disabled by default])
fi

AC_SUBST(LIBSSL_CFLAGS)
AC_SUBST(LIBSSL_LIBS)
AM_CONDITIONAL(HAVE_LIBSSL, test "x$HAVE_LIBSSL" = "xyes")

AC_CHECK_FUNC(res_query, ,
  [AC_CHECK_LIB(resolv, res_query, [ RESOLV_LIBS="-lresolv" ],
    [ save_libs="$LIBS"
      LIBS="-lresolv $LIBS"
      AC_MSG_CHECKING([for res_query in -lresolv (alternate version)])
      AC_LINK_IFELSE(
        [AC_LANG_PROGRAM([[#include <resolv.h>]], [[res_query(0,0,0,0,0)]])],
        [ AC_MSG_RESULT(yes)
          LIBS="$save_libs"
          RESOLV_LIBS="-lresolv"
        ],
        [ AC_MSG_RESULT(no)
          LIBS="$save_libs"
          AC_CHECK_LIB(bind, res_query,
            [ RESOLV_LIBS="-lbind" ],
            [ AC_MSG_ERROR(res_query not found) ] ) ] ) ] )
  ]
)

AC_SUBST(RESOLV_LIBS)

dnl Check for libasyncns
PKG_CHECK_MODULES(LIBASYNCNS, [libasyncns >= 0.3],
  [ HAVE_LIBASYNCNS="yes"
    AC_DEFINE(HAVE_LIBASYNCNS, 1, [Define if asyncns support is available])
  ],
  [ AC_MSG_NOTICE(libasyncns not found: Resolving will be synchronous!)
    AC_MSG_NOTICE((Salut does not make DNS lookups, so this does not matter.))
  ] )

AC_SUBST(LIBASYNCNS_CFLAGS)
AC_SUBST(LIBASYNCNS_LIBS)
AM_CONDITIONAL(HAVE_LIBASYNCNS, test "x$HAVE_LIBASYNCNS" = "xyes")

dnl Check for Avahi
PKG_CHECK_MODULES(AVAHI, [avahi-gobject, avahi-client])
AC_SUBST(AVAHI_CFLAGS)
AC_SUBST(AVAHI_LIBS)

dnl Check for libsoup
PKG_CHECK_MODULES(LIBSOUP, [libsoup-2.4])
AC_SUBST(LIBSOUP_CFLAGS)
AC_SUBST(LIBSOUP_LIBS)

dnl glibc backtrace functions
AC_CHECK_FUNCS(backtrace backtrace_symbols_fd)
AC_CHECK_HEADERS(execinfo.h)

dnl check for libsasl2 (for sasl test)
AC_CHECK_LIB(sasl2, sasl_server_new,
             [LIBSASL2_LIBS="-lsasl2"
              LIBSASL2_CFLAGS=""
              HAVE_LIBSASL2=yes
              AC_DEFINE(HAVE_LIBSASL2, 1,
                        [Define if libsasl2 support is available])
             ],
             [ AC_MSG_WARN(libsasl2 not found: not building sasl tests) ] )

AC_SUBST(LIBSASL2_LIBS)
AC_SUBST(LIBSASL2_CFLAGS)
AM_CONDITIONAL(HAVE_LIBSASL2, test "x$HAVE_LIBSASL2" = "xyes")

dnl check for libuuid
PKG_CHECK_MODULES([UUID], [uuid], [HAVE_UUID=yes], [HAVE_UUID=no])
AC_SUBST([UUID_CFLAGS])
AC_SUBST([UUID_LIBS])
if test x"$HAVE_UUID" = xyes; then
  AC_DEFINE([HAVE_UUID], [1], [Define if libuuid is available])
else
  AC_MSG_WARN([libuuid not found, falling back to generating random IDs])
fi

dnl signal functions
AC_CHECK_FUNCS(signal)
AC_CHECK_HEADERS(signal.h)

# Checks for the 'check' unit testing library
PKG_CHECK_MODULES([CHECK], [check >= 0.9.4], 
                   [ HAVE_CHECK=yes ],
                   [ HAVE_CHECK=no ] )
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

AC_SUBST(PACKAGE_STRING)

AC_OUTPUT( Makefile                       \
           docs/Makefile                  \
           lib/Makefile                   \
           lib/gibber/Makefile            \
           lib/gibber/examples/Makefile   \
           lib/gibber/tests/Makefile      \
           lib/gibber/tests/inputs/Makefile \
           src/Makefile                   \
           m4/Makefile                    \
           data/Makefile                  \
           extensions/Makefile            \
           tools/Makefile                 \
           tests/Makefile                 \
           tests/twisted/Makefile         \
           tests/twisted/tools/Makefile   \
)
