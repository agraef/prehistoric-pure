
dnl To regenerate the configury after changes:
dnl autoconf -I config && autoheader -I config

AC_INIT(pure, 0.6)
AC_CONFIG_AUX_DIR(config)
dnl AC_CONFIG_MACRO_DIR(config)
AC_CONFIG_HEADERS(config.h)
dnl Determine host information.
AC_CANONICAL_HOST
if test -z "${host}"; then
  host=unknown
fi
AC_DEFINE_UNQUOTED(HOST, "${host}", [Define to the name of the host system.])
AC_SUBST(host)
dnl Figure out extra build flags and filename extensions for various systems.
dnl XXXFIXME: This is probably incomplete. Please let me know about options
dnl needed for other Unix flavours.
shared="-shared"
rdynamic="-rdynamic"
PIC=
DLLEXT=".so"
AUXLIBS=
LD_LIB_PATH="LD_LIBRARY_PATH"
case "$host" in
    *-*-mingw*)       AUXLIBS="-DLIBGLOB='\"libglob.dll\"' -DLIBREGEX='\"libgnurx-0.dll\"'";
                      LIBS="$LIBS -limagehlp -lpsapi -lmingwex";
                      LDFLAGS="-Wl,--enable-auto-import"; DLLEXT=".dll";;
    x86_64-*-linux*)  PIC="-fPIC";;
    *-*-darwin*)      LD_LIB_PATH="DYLD_LIBRARY_PATH";
                      rdynamic=""; shared="-dynamiclib -install_name \$(libdir)/\$(libpure)"; DLLEXT=".dylib";;
    hppa*-hp-hpux*)   rdynamic=""; DLLEXT=".sl";;
esac
AC_SUBST(shared)
AC_SUBST(rdynamic)
AC_SUBST(PIC)
AC_SUBST(DLLEXT)
AC_SUBST(AUXLIBS)
AC_SUBST(LD_LIB_PATH)
AC_DEFINE_UNQUOTED(DLLEXT, "${DLLEXT}", [Define to the filename extension for shared libraries.])
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
    no)   LDFLAGS="$LDFLAGS $rdynamic"; sharedlib=no ;;
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
dnl Platform-dependent time functions.
AC_CHECK_FUNCS(ftime gettimeofday nanosleep usleep)
dnl Platform specifics of signal handlers.
AC_REINSTALL_SIGHANDLERS
dnl Check to see whether we have POSIX/ISOC99 complex numbers.
AC_CHECK_TYPES([_Complex float, _Complex double])
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
