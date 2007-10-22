AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set salut_release to 1.

m4_define([salut_major_version], [0])
m4_define([salut_minor_version], [1])
m4_define([salut_micro_version], [5])
m4_define([salut_nano_version], [1])

# Some magic
m4_define([salut_base_version],
          [salut_major_version.salut_minor_version.salut_micro_version])
m4_define([salut_version],
          [m4_if(salut_nano_version, 0, [salut_base_version], [salut_base_version].[salut_nano_version])])dnl

AC_INIT([Telepathy Salut], [salut_version],
[https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=telepathy-salut])

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.8 tar-ustar])
AM_PROG_LIBTOOL
AM_CONFIG_HEADER(config.h)

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_AS

dnl add common command line switches
SALUT_ARG_DEBUG
SALUT_ARG_VALGRIND
SALUT_ARG_COVERAGE

dnl decide error flags
AS_COMPILER_FLAG(-Wall, ERROR_CFLAGS="-Wall", ERROR_CFLAGS="")

ifelse(salut_nano_version, 0,
    [],
    [AS_COMPILER_FLAG(-Werror,ERROR_CFLAGS="$ERROR_CFLAGS -Werror",ERROR_CFLAGS="$ERROR_CFLAGS")
    ])
AC_SUBST(ERROR_CFLAGS)

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
PYTHON=
AC_CHECK_PROGS([PYTHON], [python2.3 python2.4 python2.5 python])
if test -z "$PYTHON"; then
  AC_MSG_ERROR([Python is required to compile this package])
fi

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

dnl Check endianness (Needed for the sha1 implementation)
AC_C_BIGENDIAN

dnl GTK docs
GTK_DOC_CHECK

dnl Check for Glib 
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.4, gobject-2.0 >= 2.4])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

dnl Check for D-Bus
PKG_CHECK_MODULES(DBUS, [dbus-1 >= 1.0.0, dbus-glib-1 >= 0.61])

AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

dnl Check for D-Bus tubes
AC_CHECK_LIB(dbus-1, dbus_message_marshal,
             [have_dbus_message_marshal=yes],
             [have_dbus_message_marshal=no],
             [$DBUS_LIBS])

have_dbus_tube=no
if test x$have_dbus_message_marshal = xyes; then
  have_dbus_tube=yes
  AC_DEFINE(ENABLE_DBUS_TUBES, 1, [Define if we have D-Bus tubes support])
fi

AM_CONDITIONAL(ENABLE_DBUS_TUBES, test x$have_dbus_tube = xyes)

if test x$have_dbus_tube = xno; then
  AC_MSG_WARN("Not building support for D-Bus tubes! (libdbus >= 1.1 required)")
fi

dnl Check for libxml2
PKG_CHECK_MODULES(LIBXML2, [libxml-2.0])

AC_SUBST(LIBXML2_CFLAGS)
AC_SUBST(LIBXML2_LIBS)

dnl Check for telepathy-glib
PKG_CHECK_MODULES(TELEPATHY_GLIB, [telepathy-glib >= 0.5.14])

AC_SUBST(TELEPATHY_GLIB_CFLAGS)
AC_SUBST(TELEPATHY_GLIB_LIBS)

dnl Check for OpenSSL
PKG_CHECK_MODULES(LIBSSL, [openssl], 
            [ HAVE_LIBSSL="yes" ],
            [ AC_WARN(openssl not found: not building gibber ssl transport) ] )

AC_SUBST(LIBSSL_CFLAGS)
AC_SUBST(LIBSSL_LIBS)
AM_CONDITIONAL(HAVE_LIBSSL, test "x$HAVE_LIBSSL" = "xyes")


AS_AC_EXPAND(DATADIR, $datadir)
DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
AC_SUBST(DBUS_SERVICES_DIR)
AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])

dnl Check for Avahi
PKG_CHECK_MODULES(AVAHI, [ avahi-client, avahi-glib ])
AC_SUBST(AVAHI_CFLAGS)
AC_SUBST(AVAHI_LIBS)

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

dnl signal functions
AC_CHECK_FUNCS(signal)
AC_CHECK_HEADERS(signal.h)

# Checks for the 'check' unit testing library
PKG_CHECK_MODULES([CHECK], [check >= 0.9.4], 
                   [ HAVE_CHECK=yes ],
                   [ HAVE_CHECK=no ] )
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

AC_OUTPUT( Makefile                       \
           lib/Makefile                   \
           lib/gibber/Makefile            \
           lib/gibber/examples/Makefile   \
           src/Makefile                   \
           m4/Makefile                    \
           data/Makefile                  \
           extensions/Makefile            \
           extensions/tools/Makefile      \
           tests/Makefile                 \
           tests/inputs/Makefile
)
