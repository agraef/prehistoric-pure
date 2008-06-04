
AC_INIT(pure, 0.3)
AC_CONFIG_HEADERS(config.h)
AC_CANONICAL_HOST
if test -z "${host}"; then
  host=unknown
fi
AC_DEFINE_UNQUOTED(HOST, "${host}", [Define to the canonical host system name.])
AC_SUBST(host)
dnl Figure out extra build flags and filename extensions for various systems.
RDYNAMIC=
DLLEXT=".so"
case "$host" in
    *-*-mingw*)       RDYNAMIC="-rdynamic"; LIBS= "-limagehlp -lpsapi";
                      DLLEXT=".dll";;
    *-*-linux*)       RDYNAMIC="-rdynamic";;
    *-*-freebsd*)     RDYNAMIC="-rdynamic";;
    *-*-darwin*)      DLLEXT=".dylib";;
    hppa*-hp-hpux*)   DLLEXT=".sl";;
esac
AC_SUBST(RDYNAMIC)
AC_SUBST(DLLEXT)
AC_PROG_CXX
AC_ARG_ENABLE(release,
  [  --enable-release        enable the release build],
  [case "${enableval}" in
    yes)  CPPFLAGS="-DNDEBUG -DDEBUG=0"; CXXFLAGS="-O3" ;;
  esac])
AC_ARG_ENABLE(debug,
  [  --enable-debug          enable the debug build],
  [case "${enableval}" in
    yes)  CXXFLAGS="-g" ;;
  esac])
AC_ARG_ENABLE(debug2,
  [  --enable-debug2         enable the maintenance build],
  [case "${enableval}" in
    yes)  CPPFLAGS="-DDEBUG=2"; CXXFLAGS="-g" ;;
  esac])
SHARED=yes
AC_ARG_ENABLE(shared,
  [  --disable-shared        link the interpreter statically],
  [case "${enableval}" in
    no)   LDFLAGS="$LDFLAGS $RDYNAMIC"; SHARED=no ;;
  esac])
AC_SUBST(SHARED)
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
