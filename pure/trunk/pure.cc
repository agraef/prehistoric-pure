
/* The Pure main program. This is currently rather simplistic. See the README
   file and the man page for details. */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "interpreter.hh"
#include "runtime.h"
#include <llvm/Target/TargetOptions.h>

#include "config.h"

using namespace std;

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.0"
#endif
#ifndef PURELIB
#define PURELIB "/usr/local/lib/pure"
#endif

#define COPYRIGHT "Copyright (c) 2008 by Albert Graef"
#define USAGE \
"Usage: pure [-h] [-i] [-n] [-v[level]] [script ...] [-- args ...]\n\
-h: Print this message and exit.\n\
-i: Force interactive mode (read commands from stdin).\n\
-n: Suppress automatic inclusion of the prelude.\n\
-v: Set verbosity level (useful for debugging purposes).\n\
--: Stop option processing, pass remaining args in argv variable.\n\
Environment:\n\
PURELIB:    Directory to search for source scripts including the prelude.\n\
PURE_PS:    Command prompt to be used in the interactive command loop.\n\
PURE_STACK: Maximum stack size in kilobytes (default: 0 = unlimited).\n"
#define LICENSE "This program is free software distributed under the GNU Public License\n(GPL V3 or later). Please see the COPYING file for details.\n"

const char *commands[] = {
  "cd", "clear", "extern", "help", "infix", "infixl", "infixr", "let", "list",
  "ls", "nullary", "override", "postfix", "prefix", "pwd", "quit", "run",
  "save", "stats", "underride", "using", 0
};

/* Generator functions for command completion. */

static char *
command_generator(const char *text, int state)
{
  static int list_index, len;
  static env::iterator it, end;
  const char *name;

  /* New match. */
  if (!state) {
    list_index = 0;
    assert(interpreter::g_interp);
    it = interpreter::g_interp->globenv.begin();
    end = interpreter::g_interp->globenv.end();
    len = strlen(text);
  }

  /* Return the next name which partially matches from the
     command list. */
  while ((name = commands[list_index])) {
    list_index++;
    if (strncmp(name, text, len) == 0)
      return strdup(name);
  }

  /* Return the next name which partially matches from the
     symbol list. */
  while (it != end) {
    assert(it->first > 0);
    symbol& sym = interpreter::g_interp->symtab.sym(it->first);
    it++;
    if (strncmp(sym.s.c_str(), text, len) == 0)
      return strdup(sym.s.c_str());
  }

  /* If no names matched, then return NULL. */
  return 0;
}

static char *
symbol_generator(const char *text, int state)
{
  static int len;
  static env::iterator it, end;

  /* New match. */
  if (!state) {
    assert(interpreter::g_interp);
    it = interpreter::g_interp->globenv.begin();
    end = interpreter::g_interp->globenv.end();
    len = strlen(text);
  }

  /* Return the next name which partially matches from the
     symbol list. */
  while (it != end) {
    assert(it->first > 0);
    symbol& sym = interpreter::g_interp->symtab.sym(it->first);
    it++;
    if (strncmp(sym.s.c_str(), text, len) == 0)
      return strdup(sym.s.c_str());
  }

  /* If no names matched, then return NULL. */
  return 0;
}

static char **
pure_completion(const char *text, int start, int end)
{
  char **matches;

  matches = (char **)NULL;

  /* If this word is at the start of the line, then it is a command to
     complete. Otherwise it is a global function or variable symbol, or the
     name of a file in the current directory. */
  if (start == 0)
    matches = rl_completion_matches(text, command_generator);
  else
    matches = rl_completion_matches(text, symbol_generator);

  return matches;
}

static void sigint_handler(int sig)
{
  interpreter::brkflag = sig;
}

static const char *histfile = 0;

static void exit_handler()
{
  if (histfile) write_history(histfile);
}

int
main(int argc, char *argv[])
{
  char base;
  interpreter interp;
  int count = 0;
  bool force_interactive = false, want_prelude = true, have_prelude = false;
  // This is used in advisory stack checks.
  interpreter::baseptr = &base;
  // make sure that SIGPIPE is ignored
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif
  /* Set up a SIGINT handler which throws a Pure exception, so that we safely
     return to the interpreter's interactive command line when the user
     interrupts a computation. */
  signal(SIGINT, sigint_handler);
  // set up an exit function which saves the history if needed
  atexit(exit_handler);
  // set the system locale
  setlocale(LC_ALL, "");
  // get some settings from the environment
  const char *env;
  if ((env = getenv("HOME")))
    interp.histfile = string(env)+"/.pure_history";
  if ((env = getenv("PURE_PS")))
    interp.ps = string(env);
  if ((env = getenv("PURE_STACK"))) {
    char *end;
    size_t n = strtoul(env, &end, 0);
    if (!*end) interpreter::stackmax = n*1024;
  }
  if ((env = getenv("PURELIB")))
    interp.lib = string(env)+"/";
  else
    interp.lib = string(PURELIB)+"/";
  string prelude = interp.lib+string("prelude.pure");
#if USE_FASTCC
  // This global option is needed to get tail call optimization (you'll also
  // need to have USE_FASTCC in interpreter.hh enabled).
  llvm::PerformTailCallOpt = true;
#endif
  // scan the command line options
  const string prog = *argv;
  list<string> myargs;
  for (char **args = ++argv; *args; ++args)
    if (*args == string("-h")) {
      cout << "Pure " << PACKAGE_VERSION << " " << COPYRIGHT << endl << USAGE;
      return 0;
    } else if (*args == string("-i"))
      force_interactive = true;
    else if (*args == string("-n"))
      want_prelude = false;
    else if (string(*args).substr(0,2) == "-v") {
      string s = string(*args).substr(2);
      if (s.empty()) continue;
      char *end;
      strtoul(s.c_str(), &end, 0);
      if (*end) {
	interp.error(prog + ": invalid option " + *args);
	return 1;
      }
    } else if (*args == string("--")) {
      while (*++args) myargs.push_back(*args);
      break;
    } else if (**args == '-') {
      interp.error(prog + ": invalid option " + *args);
      return 1;
    }
  interp.init_sys_vars(PACKAGE_VERSION, myargs);
  if (want_prelude) {
    // load the prelude if we can find it
    FILE *fp = fopen("prelude.pure", "r");
    if (fp)
      prelude = "prelude.pure";
    else
      // try again in the PURELIB directory
      fp = fopen(prelude.c_str(), "r");
    if (fp) {
      fclose(fp);
      have_prelude = true;
      interp.run(prelude);
      interp.compile();
    }
  }
  // load scripts specified on the command line
  for (; *argv; ++argv)
    if (string(*argv).substr(0,2) == "-v") {
      uint8_t level = 1;
      string s = string(*argv).substr(2);
      if (!s.empty()) level = (uint8_t)strtoul(s.c_str(), 0, 0);
      interp.verbose = level;
    } else if (*argv == string("--"))
      break;
    else if (**argv == '-')
      ;
    else if (**argv) {
      if (count++ == 0) interp.modname = *argv;
      interp.run(*argv);
    }
  if (count > 0 && !force_interactive) {
    if (interp.verbose&verbosity::dump) interp.compile();
    return 0;
  }
  interp.symtab.init_builtins();
  // enter the interactive command loop
  interp.interactive = true;
  if (isatty(fileno(stdin))) {
    // connected to a terminal, print sign-on and initialize readline
    cout << "Pure " << PACKAGE_VERSION << " " << COPYRIGHT << endl << LICENSE;
    if (have_prelude)
      cout << "Loaded prelude from " << prelude << ".\n\n";
    rl_readline_name = "Pure";
    rl_attempted_completion_function = pure_completion;
    using_history();
    read_history(interp.histfile.c_str());
    stifle_history(600);
    histfile = strdup(interp.histfile.c_str());
    interp.compile();
    interp.ttymode = true;
  }
  interp.temp = 1;
  interp.run("");
  if (interp.ttymode) cout << endl;
  return 0;
}
