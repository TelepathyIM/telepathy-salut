dnl Detect and set flags for gcov

AC_DEFUN([SALUT_GCOV],
[
  enable=$1

  GCOV=`echo $CC | sed s/gcc/gcov/g`
  AC_CHECK_PROG(have_gcov, $GCOV, yes, no)

  AS_COMPILER_FLAG(["-fprofile-arcs"],
    [flag_profile_arcs=yes],
    [flag_profile_arcs=no])

  AS_COMPILER_FLAG(["-ftest-coverage"],
    [flag_test_coverage=yes],
    [flag_test_coverage=no])

  if test "x$enable" = xyes &&
     test "x$GCC" = "xyes" &&
     test "$flag_profile_arcs" = yes &&
     test "$flag_test_coverage" = yes ;
  then

    GCOV_CFLAGS="$GCOV_CFLAGS -fprofile-arcs -ftest-coverage"
    dnl remove any -O flags - FIXME: is this needed ?
    GCOV_CFLAGS=`echo "$GCOV_CFLAGS" | sed -e 's/-O[[0-9]]*//g'`

    dnl libtool 1.5.22 and lower strip -fprofile-arcs from the flags
    dnl passed to the linker, which is a bug; -fprofile-arcs implicitly
    dnl links in -lgcov, so we do it explicitly here for the same effect
    GCOV_LIBS=-lgcov

    AC_SUBST([MOSTLYCLEANFILES], "*.bb *.bbg *.da *.gcov *.gcda *.gcno")

    AC_DEFINE_UNQUOTED(HAVE_GCOV, 1,
      [Defined if gcov is enabled to force a rebuild due to config.h changing])

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

  AC_SUBST(GCOV_CFLAGS)
  AC_SUBST(GCOV_LIBS)
  AC_SUBST(GCOV)

  AM_CONDITIONAL(HAVE_GCOV, test x$have_gcov = xyes)
])
