dnl Detect and set flags for gcov

AC_DEFUN([SALUT_GCOV],
[
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

  HAVE_GCOV=yes

  AC_DEFINE_UNQUOTED(HAVE_GCOV, 1,
    [Defined if gcov is enabled to force a rebuild due to config.h changing])

  dnl if gcov is used, we do not want default -O2 CFLAGS
  if test "x$HAVE_GCOV" = "xyes"
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

  AM_CONDITIONAL(HAVE_GCOV, test x$HAVE_GCOV = xyes)
])
