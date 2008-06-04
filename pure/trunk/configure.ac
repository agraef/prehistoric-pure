
AC_INIT(pure, 0.3)
AC_CONFIG_HEADERS(config.h)
dnl Determine host information.
AC_CANONICAL_HOST
if test -z "${host}"; then
  host=unknown
fi
AC_DEFINE_UNQUOTED(HOST, "${host}", [Define to the name of the host system.])
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
dnl Check for programs.
AC_PROG_INSTALL
AC_PROG_CXX
dnl Parse --enable options.
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
dnl Check for libraries.
AC_CHECK_LIB(gmp, __gmpz_init)
AC_CHECK_LIB(readline, readline)
dnl On some systems these are in separate libraries.
AC_CHECK_LIB(glob, glob)
AC_CHECK_LIB(regex, regcomp)
dnl iconv and nl_langinfo need special treatment (macros by Bruno Haible).
AM_ICONV
AM_LANGINFO_CODESET
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_RESULT([
Pure is now configured for ${host}.

  Source directory:     ${srcdir}
  Installation prefix:  ${prefix}
  Compiler:             $CXX $CXXFLAGS $CPPFLAGS
  Linker:               $CXX $LDFLAGS $LIBS
  Build libpure:        $SHARED

Now run 'make' to build everything, and 'make install' to install this
software on your system. To remove the installed software at a later
time use the 'make uninstall' command.
])
