dnl Detect Valgrind location and flags

AC_DEFUN([SALUT_VALGRIND],
[
  VALGRIND_REQ="2.1"
  PKG_CHECK_MODULES(VALGRIND, valgrind > $VALGRIND_REQ,
    use_valgrind="yes",
    [
      use_valgrind="no"
      AC_MSG_RESULT([no])
    ])

  if test "x$use_valgrind" = xyes; then
    AC_DEFINE(HAVE_VALGRIND, 1, [Define if valgrind should be used])
    AC_MSG_NOTICE(Using extra code paths for valgrind)
  fi

  AC_SUBST(VALGRIND_CFLAGS)
  AC_SUBST(VALGRIND_LIBS)
  
  AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
  AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")
])
