dnl configure-time options shared among gstreamer modules

dnl SALUT_ARG_DEBUG
dnl SALUT_ARG_PROFILING
dnl SALUT_ARG_VALGRIND
dnl SALUT_ARG_GCOV

AC_DEFUN([SALUT_ARG_DEBUG],
[
  dnl debugging stuff
  AC_ARG_ENABLE(debug,
    AC_HELP_STRING([--disable-debug],[disable addition of -g debugging info]),
    [
      case "${enableval}" in
        yes) USE_DEBUG=yes ;;
        no)  USE_DEBUG=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
      esac
    ],
    [USE_DEBUG=yes]) dnl Default value
])

AC_DEFUN([SALUT_ARG_PROFILING],
[
  AC_ARG_ENABLE(profiling,
    AC_HELP_STRING([--enable-profiling],
      [adds -pg to compiler commandline, for profiling]),
    [
      case "${enableval}" in
        yes) USE_PROFILING=yes ;;
        no)  USE_PROFILING=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
      esac
    ], 
    [USE_PROFILING=no]) dnl Default value
])

AC_DEFUN([SALUT_ARG_VALGRIND],
[
  dnl valgrind inclusion
  AC_ARG_ENABLE(valgrind,
    AC_HELP_STRING([--disable-valgrind],[disable run-time valgrind detection]),
    [
      case "${enableval}" in
        yes) USE_VALGRIND="$USE_DEBUG" ;;
        no)  USE_VALGRIND=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-valgrind) ;;
      esac
    ],
    [
      USE_VALGRIND=no
    ])

  VALGRIND_REQ="2.1"
  if test "x$USE_VALGRIND" = xyes; then
    PKG_CHECK_MODULES(VALGRIND, valgrind > $VALGRIND_REQ,
      USE_VALGRIND="yes",
      [
        USE_VALGRIND="no"
        AC_MSG_RESULT([no])
      ])
  fi

  if test "x$USE_VALGRIND" = xyes; then
    AC_DEFINE(HAVE_VALGRIND, 1, [Define if valgrind should be used])
    AC_MSG_NOTICE(Using extra code paths for valgrind)
  fi

  AC_SUBST(VALGRIND_CFLAGS)
  AC_SUBST(VALGRIND_LIBS)
  
  AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
  AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")
])

AC_DEFUN([SALUT_ARG_GCOV],
[
  AC_ARG_ENABLE(gcov,
    AC_HELP_STRING([--enable-gcov],
      [compile with coverage profiling instrumentation (gcc only)]),
    enable_gcov=$enableval,
    enable_gcov=no)
  if test x$enable_gcov = xyes ; then
    if test "x$GCC" != "xyes"
    then
      AC_MSG_ERROR([gcov only works if gcc is used])
    fi

    AS_COMPILER_FLAG(["-fprofile-arcs"],
      [GCOV_CFLAGS="$GCOV_CFLAGS -fprofile-arcs"],
      true)
    AS_COMPILER_FLAG(["-ftest-coverage"],
      [GCOV_CFLAGS="$GCOV_CFLAGS -ftest-coverage"],
      true)
    dnl remove any -O flags - FIXME: is this needed ?
    GCOV_CFLAGS=`echo "$GCOV_CFLAGS" | sed -e 's/-O[[0-9]]*//g'`
    dnl libtool 1.5.22 and lower strip -fprofile-arcs from the flags
    dnl passed to the linker, which is a bug; -fprofile-arcs implicitly
    dnl links in -lgcov, so we do it explicitly here for the same effect
    GCOV_LIBS=-lgcov
    AC_SUBST(GCOV_CFLAGS)
    AC_SUBST(GCOV_LIBS)
    GCOV=`echo $CC | sed s/gcc/gcov/g`
    AC_SUBST(GCOV)

    AC_SUBST([MOSTLYCLEANFILES], "*.bb *.bbg *.da *.gcov *.gcda *.gcno")

    GCOV_ENABLED=yes
    AC_DEFINE_UNQUOTED(GCOV_ENABLED, 1,
      [Defined if gcov is enabled to force a rebuild due to config.h changing])
    dnl if gcov is used, we do not want default -O2 CFLAGS
    if test "x$GCOV_ENABLED" = "xyes"
    then
      CFLAGS="-O0"
      AC_SUBST(CFLAGS)
      CXXFLAGS="-O0"
      AC_SUBST(CXXFLAGS)
      FFLAGS="-O0"
      AC_SUBST(FFLAGS)
      CCASFLAGS="-O0"
      AC_SUBST(CCASFLAGS)
      AC_MSG_NOTICE([gcov enabled, setting CFLAGS and friends to $CFLAGS])
    fi

    dnl Check for lcov utility
    AC_PATH_PROG(LCOV_PATH, [lcov], [false])
    if test X$LCOV_PATH != Xfalse ; then
      AC_MSG_CHECKING([whether lcov accepts --compat-libtool])
      if $LCOV_PATH --compat-libtool --help > /dev/null 2>&1 ; then
        AC_MSG_RESULT(ok)
      else
        AC_MSG_RESULT(not supported)
        AC_MSG_WARN([lcov option --compat-libtool is not supported])
        AC_MSG_WARN([Update lcov to version > 1.5])
        AC_MSG_WARN([will use an internal lcov copy])
        LCOV_PATH='$(top_srcdir)/scripts/lcov/lcov'
      fi
    fi
  fi
  AM_CONDITIONAL(GCOV_ENABLED, test x$enable_gcov = xyes)
])
