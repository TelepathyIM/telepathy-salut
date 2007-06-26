dnl configure-time options for Telepathy Salut

dnl SALUT_ARG_DEBUG
dnl SALUT_ARG_PROFILING
dnl SALUT_ARG_VALGRIND
dnl SALUT_ARG_COVERAGE

AC_DEFUN([SALUT_ARG_DEBUG],
[
  dnl debugging stuff
  AC_ARG_ENABLE(debug,
    AC_HELP_STRING([--disable-debug],[compile without debug code]),
    [
      case "${enableval}" in
        yes|no) enable="${enableval}" ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
      esac
    ],
    [enable=yes])

  if test "$enable" = yes; then
    AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
  fi
])

AC_DEFUN([SALUT_ARG_PROFILING],
[
  AC_ARG_ENABLE(profiling,
    AC_HELP_STRING([--enable-profiling],
      [adds -pg to compiler commandline, for profiling]),
    [
      case "${enableval}" in
        yes) ENABLE_PROFILING=yes ;;
        no)  ENABLE_PROFILING=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
      esac
    ], 
    [ENABLE_PROFILING=no]) dnl Default value
])

AC_DEFUN([SALUT_ARG_VALGRIND],
[
  dnl valgrind inclusion
  AC_ARG_ENABLE(valgrind,
    AC_HELP_STRING([--enable-valgrind],[enable valgrind checking and run-time detection]),
    [
      case "${enableval}" in
        yes|no) enable="${enableval}" ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-valgrind) ;;
      esac
    ],
    [enable=no])

  SALUT_VALGRIND($enable, [2.1])
])

AC_DEFUN([SALUT_ARG_COVERAGE],
[
  AC_ARG_ENABLE(coverage,
    AC_HELP_STRING([--enable-coverage],
      [compile with coverage profiling instrumentation (gcc only)]),
    [
      case "${enableval}" in
        yes|no) enable="${enableval}" ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-coverage) ;;
      esac
    ],
    [enable=no])

  SALUT_GCOV($enable)
  SALUT_LCOV($enable)
])
