
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
dnl XXXFIXME: -fPIC is acurrently assumed for Linux x86-64 only. There might
dnl be other Unix systems which need this.
PIC=
RDYNAMIC=
DLLEXT=".so"
AUX_LIBS=
case "$host" in
    *-*-mingw*)       RDYNAMIC="-rdynamic"; DLLEXT=".dll";
                      AUXLIBS="-DLIBGLOB='\"libglob.dll\"' -DLIBREGEX='\"libgnurx-0.dll\"'";
                      LIBS="$LIBS -limagehlp -lpsapi";
                      LDFLAGS="-Wl,--enable-auto-import";;
    x86_64-*-linux*)  RDYNAMIC="-rdynamic"; PIC="-fPIC";;
    *-*-linux*)       RDYNAMIC="-rdynamic";;
    *-*-freebsd*)     RDYNAMIC="-rdynamic";;
    *-*-darwin*)      DLLEXT=".dylib";;
    hppa*-hp-hpux*)   DLLEXT=".sl";;
esac
AC_SUBST(PIC)
AC_SUBST(RDYNAMIC)
AC_SUBST(DLLEXT)
AC_SUBST(AUXLIBS)
dnl Check for programs.
AC_PROG_INSTALL
AC_PROG_CXX
dnl Determine pointer sizes. This will be 8 on 64 bit systems.
AC_CHECK_SIZEOF(void *)
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
sharedlib=yes
AC_ARG_ENABLE(shared,
  [  --disable-shared        link the interpreter statically],
  [case "${enableval}" in
    no)   LDFLAGS="$LDFLAGS $RDYNAMIC"; sharedlib=no ;;
  esac])
AC_SUBST(sharedlib)
AC_ARG_ENABLE(warnings,
  [  --enable-warnings       enable compiler warnings (-Wall)],
  [case "${enableval}" in
    yes)  CXXFLAGS="$CXXFLAGS -Wall" ;;
  esac])
dnl Check for libraries.
AC_CHECK_LIB(gmp, __gmpz_init)
AC_CHECK_LIB(readline, readline)
dnl On some systems iconv is in a separate library, and may actually be named
dnl libiconv.
AC_CHECK_LIB(iconv, libiconv)
if test $ac_cv_lib_iconv_libiconv = no; then
  AC_CHECK_LIB(iconv, iconv)
fi
dnl On non-POSIX systems like Windows, we have to get the glob and regex
dnl functions from separate libraries, too.
AC_CHECK_LIB(glob, glob)
AC_CHECK_LIB(regex, regcomp)
dnl iconv and nl_langinfo need special treatment (macros by Bruno Haible).
AM_ICONV
AM_LANGINFO_CODESET
dnl Determine how to get alloca.
AC_FUNC_ALLOCA
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_RESULT([
Pure is now configured for ${host}.

  Source directory:     ${srcdir}
  Installation prefix:  ${prefix}
  Compiler:             $CXX $CXXFLAGS $CPPFLAGS
  Linker:               $CXX $LDFLAGS $LIBS
  Build libpure:        $sharedlib

Now run 'make' to build everything, and 'make install' to install this
software on your system. To remove the installed software at a later
time use the 'make uninstall' command.
])
