dnl Check for lcov utility

AC_DEFUN([SALUT_LCOV],
[
  AC_PATH_PROG(LCOV_PATH, [lcov], [no])

  if test X$LCOV_PATH != Xno ; then
    AC_MSG_CHECKING([whether lcov accepts --compat-libtool])
    if $LCOV_PATH --compat-libtool --help > /dev/null 2>&1 ; then
      AC_MSG_RESULT(ok)
    else
      AC_MSG_WARN([lcov option --compat-libtool is not supported])
      AC_MSG_WARN([update lcov to version > 1.5])
      LCOV_PATH=no
    fi
  fi

  if test X$LCOV_PATH == Xno ; then
    AC_MSG_WARN([will use an internal lcov copy])
    LCOV_PATH='$(top_srcdir)/scripts/lcov/lcov'
  fi
])
