
/* Poor man's Pure interpreter. 2008-06-24 AG */

/* This is an example for calling Pure from C/C++ applications. Using the
   public runtime API of Pure 0.5 or later, it implements a little command
   loop which reads lines of Pure code from standard input, evaluates them and
   prints the results, until it encounters end-of-file.

   Compile this with 'gcc -o poor poor.c -lpure', and run the resulting
   executable as './poor [args ...]'. You can use the same command line
   arguments as with the real Pure interpreter, including any Pure scripts to
   be loaded at startup. Input is line-oriented, so you can't continue
   definitions across lines, but in return you don't have to terminate each
   line with a ';' either, the eval() function already takes care of that.

   Please note that the interpreter interface provided by the runtime API is
   rather minimalistic right now. In particular, the interpreter always runs
   in non-interactive mode (thus none of the interactive commands will work)
   and eval() only returns the result of the last computed expression (this is
   what gets printed in the read-eval-print loop). */

#include <stdio.h>
#include <pure/runtime.h>

int main(int argc, char *argv[])
{
  pure_interp *interp = pure_create_interp(argc, argv);
  char buf[10000];
  fputs("? ", stdout); fflush(stdout);
  while (fgets(buf, sizeof(buf), stdin)) {
    pure_expr *x = eval(buf);
    if (x) {
      printf("%s\n", str(x));
      pure_freenew(x);
    } else if (lasterr())
      fputs(lasterr(), stderr);
    fputs("? ", stdout); fflush(stdout);
  }
  puts("[quit]");
  return 0;
}
