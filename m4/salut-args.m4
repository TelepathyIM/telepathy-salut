dnl configure-time options for Telepathy Salut

dnl SALUT_ARG_DEBUG
dnl SALUT_ARG_PROFILING
dnl SALUT_ARG_VALGRIND
dnl SALUT_ARG_COVERAGE

AC_DEFUN([SALUT_ARG_DEBUG],
[
  dnl debugging stuff
  AC_ARG_ENABLE(debug,
    AC_HELP_STRING([--disable-debug],[disable addition of -g debugging info]),
    [
      case "${enableval}" in
        yes) ENABLE_DEBUG=yes ;;
        no)  ENABLE_DEBUG=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
      esac
    ],
    [ENABLE_DEBUG=yes]) dnl Default value
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
    AC_HELP_STRING([--disable-valgrind],[disable run-time valgrind detection]),
    [
      case "${enableval}" in
        yes) ENABLE_VALGRIND=yes ;;
        no)  ENABLE_VALGRIND=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-valgrind) ;;
      esac
    ],
    [ENABLE_VALGRIND=no])
])

AC_DEFUN([SALUT_ARG_COVERAGE],
[
  AC_ARG_ENABLE(coverage,
    AC_HELP_STRING([--enable-coverage],
      [compile with coverage profiling instrumentation (gcc only)]),
    [
      case "${enableval}" in
        yes) ENABLE_COVERAGE=yes ;;
        no)  ENABLE_COVERAGE=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-coverage) ;;
      esac
    ],
    [ENABLE_COVERAGE=no])
])
