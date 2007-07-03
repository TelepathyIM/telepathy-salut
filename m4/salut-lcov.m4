dnl Check for lcov utility

AC_DEFUN([SALUT_LCOV],
[
  enable=$1

  AC_CHECK_PROGS(LCOV_PATH, lcov)

  if test -n "$LCOV_PATH" ; then
    AC_MSG_CHECKING([whether lcov accepts --compat-libtool])
    if $LCOV_PATH --compat-libtool --help > /dev/null 2>&1 ; then
      AC_MSG_RESULT(ok)
    else
      AC_MSG_RESULT(no)
      AC_MSG_WARN([lcov option --compat-libtool is not supported])
      AC_MSG_WARN([update lcov to version > 1.5])
      LCOV_PATH=""
    fi
  fi

  AM_CONDITIONAL(HAVE_LCOV, test -n "$LCOV_PATH" && test "x$enable" = xyes)
])
