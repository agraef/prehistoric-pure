
AC_INIT(pure, 0.3)
AC_CONFIG_HEADERS(config.h)
AC_PROG_CXX
AC_ARG_ENABLE(release,
  [  --enable-release        enable the release build],
  [case "${enableval}" in
    yes)  CXXFLAGS="-O3 -DNDEBUG -DDEBUG=0" ;;
  esac])
AC_ARG_ENABLE(debug,
  [  --enable-debug          enable the debug build],
  [case "${enableval}" in
    yes)  CXXFLAGS="-g" ;;
  esac])
AC_ARG_ENABLE(debug2,
  [  --enable-debug2         enable the maintenance build],
  [case "${enableval}" in
    yes)  CXXFLAGS="-g -DDEBUG=2" ;;
  esac])
AC_CHECK_LIB(gmp, __gmpz_init)
AC_CHECK_LIB(readline, readline)
dnl On some systems these are in separate libraries.
AC_CHECK_LIB(glob, glob)
AC_CHECK_LIB(regex, regcomp)
dnl iconv needs special treatment (macro pilfered from automake).
AM_ICONV
AM_LANGINFO_CODESET
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
