
/* Poor man's Pure interpreter. */

/* This is an example for the C/C++->Pure interface. It implements a silly
   little command loop which reads Pure code, evaluates it, and prints the
   results. Compile this with 'gcc -o poor poor.c -lpure', and run as
   './poor args...'. You can use the same command line options as with the
   real Pure interpreter, including any Pure scripts to be loaded at startup.

   Please note that the interface to the interpreter, as provided by the
   public runtime API, is rather minimalistic right now. In particular, the
   interpreter will always run in non-interactive mode (thus none of the
   interactive commands will work) and eval() will only return the last
   computed expression. */

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
