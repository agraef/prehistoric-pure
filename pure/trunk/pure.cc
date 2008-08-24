
/* The Pure main program. This is currently rather simplistic. See the README
   file and the man page for details. */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
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

#ifndef HOST
#define HOST "unknown"
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.0"
#endif
#ifndef PURELIB
#define PURELIB "/usr/local/lib/pure-" PACKAGE_VERSION
#endif

#define COPYRIGHT "Copyright (c) 2008 by Albert Graef"
#define USAGE \
"Usage:        pure [options ...] [script ...] [-- args ...]\n\
              pure [options ...] -x script [args ...]\n\
Options:\n\
-h            Print this message and exit.\n\
-i            Force interactive mode (read commands from stdin).\n\
-Idirectory   Add directory to search for included source files.\n\
-Ldirectory   Add directory to search for dynamic libraries.\n\
-n            Suppress automatic inclusion of the prelude.\n\
-q            Quiet startup (suppresses sign-on message).\n\
-v[level]     Set debugging level (default: 1).\n\
-x            Execute script with given command line arguments.\n\
--            Stop option processing, pass remaining args in argv variable.\n\
Environment:\n\
PURELIB       Directory to search for library scripts and the prelude.\n\
PURE_INCLUDE  Path to search for included source files.\n\
PURE_LIBRARY  Path to search for dynamic libraries.\n\
PURE_MORE     Shell command for paging through output of the 'list' command.\n\
PURE_PS       Command prompt to be used in the interactive command loop.\n\
PURE_STACK    Maximum stack size in kilobytes (default: 0 = unlimited).\n"
#define LICENSE "This program is free software distributed under the GNU Public License\n(GPL V3 or later). Please see the COPYING file for details.\n"

static const char *commands[] = {
  "cd", "clear", "const", "def", "extern", "help", "infix", "infixl",
  "infixr", "let", "list", "ls", "nullary", "override", "postfix", "prefix",
  "pwd", "quit", "run", "save", "stats", "underride", "using", 0
};

/* Generator functions for command completion. */

typedef map<string, symbol> symbol_map;

static char *
command_generator(const char *text, int state)
{
  static int list_index, len;
  static symbol_map::iterator it, end;
  const char *name;
  assert(interpreter::g_interp);
  interpreter& interp = *interpreter::g_interp;

  /* New match. */
  if (!state) {
    list_index = 0;
    /* Must do this here, so that symbols are entered into the globalvars
       table. */
    interp.compile();
    it = interp.symtab.tab.begin();
    end = interp.symtab.tab.end();
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
    int32_t f = it->second.f;
    /* Skip non-toplevel symbols. */
    if (interp.globenv.find(f) == interp.globenv.end() &&
	interp.globalvars.find(f) == interp.globalvars.end() &&
	interp.externals.find(f) == interp.externals.end()) {
      it++;
      continue;
    }
    const string& s = it->first;
    it++;
    if (strncmp(s.c_str(), text, len) == 0)
      return strdup(s.c_str());
  }

  /* If no names matched, then return NULL. */
  return 0;
}

static char *
symbol_generator(const char *text, int state)
{
  static int len;
  static symbol_map::iterator it, end;
  assert(interpreter::g_interp);
  interpreter& interp = *interpreter::g_interp;

  /* New match. */
  if (!state) {
    /* Must do this here, so that symbols are entered into the globalvars
       table. */
    interp.compile();
    it = interp.symtab.tab.begin();
    end = interp.symtab.tab.end();
    len = strlen(text);
  }

  /* Return the next name which partially matches from the
     symbol list. */
  while (it != end) {
    int32_t f = it->second.f;
    /* Skip non-toplevel symbols. */
    if (interp.globenv.find(f) == interp.globenv.end() &&
	interp.globalvars.find(f) == interp.globalvars.end() &&
	interp.externals.find(f) == interp.externals.end()) {
      it++;
      continue;
    }
    const string& s = it->first;
    it++;
    if (strncmp(s.c_str(), text, len) == 0)
      return strdup(s.c_str());
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

static void sig_handler(int sig)
{
  interpreter::brkflag = sig;
}

static const char *histfile = 0;

static void exit_handler()
{
  if (histfile) write_history(histfile);
}

static inline bool chkfile(const string& s)
{
  struct stat st;
  return !stat(s.c_str(), &st) && !S_ISDIR(st.st_mode);
}

static void add_path(list<string>& dirs, const string& path)
{
  size_t pos = 0;
  while (pos != string::npos) {
#ifdef _WIN32
    size_t end = path.find(';', pos);
#else
    size_t end = path.find(':', pos);
#endif
    string s;
    if (end == string::npos) {
      s = path.substr(pos);
      pos = end;
    } else {
      s = path.substr(pos, end-pos);
      pos = end+1;
    }
    if (!s.empty()) {
      if (s[s.size()-1] != '/') s.append("/");
      dirs.push_back(s);
    }
  }
}

static string unixize(const string& s)
{
  string t = s;
#ifdef _WIN32
  for (size_t i = 0, n = t.size(); i<n; i++)
    if (t[i] == '\\')
      t[i] = '/';
#endif
  return t;
}

int
main(int argc, char *argv[])
{
  char base;
  interpreter interp;
  int count = 0;
  bool quiet = false, force_interactive = false,
    want_prelude = true, have_prelude = false;
  // This is used in advisory stack checks.
  interpreter::baseptr = &base;
  /* Set up handlers for all standard POSIX termination signals (except
     SIGKILL which is unmaskable). SIGPIPE is ignored by default, all others
     are mapped to Pure exceptions of the form 'signal SIG', so that they can
     be caught with 'catch' or safely return us to the interpreter's
     interactive command line. */
#ifdef SIGHUP
  signal(SIGHUP, sig_handler);
#endif
#ifdef SIGINT
  signal(SIGINT, sig_handler);
#endif
#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif
#ifdef SIGALRM
  signal(SIGALRM, sig_handler);
#endif
#ifdef SIGTERM
  signal(SIGTERM, sig_handler);
#endif
#ifdef SIGUSR1
  signal(SIGUSR1, sig_handler);
#endif
#ifdef SIGUSR2
  signal(SIGUSR2, sig_handler);
#endif
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
  if ((env = getenv("PURELIB"))) {
    string s = unixize(env);
    if (!s.empty() && s[s.size()-1] != '/') s.append("/");
    interp.libdir = s;
  } else
    interp.libdir = string(PURELIB)+"/";
  string prelude = interp.libdir+string("prelude.pure");
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
      cout << "Pure " << PACKAGE_VERSION << " (" << HOST << ") "
	   << COPYRIGHT << endl << USAGE;
      return 0;
    } else if (*args == string("-i"))
      force_interactive = true;
    else if (*args == string("-n"))
      want_prelude = false;
    else if (*args == string("-q"))
      quiet = true;
    else if (string(*args).substr(0,2) == "-I") {
      string s = string(*args).substr(2);
      if (s.empty()) {
	if (!*++args) {
	  interp.error(prog + ": -I lacks directory argument");
	  return 1;
	}
	s = *args;
      }
      s = unixize(s);
      if (!s.empty()) {
	if (s[s.size()-1] != '/') s.append("/");
	interp.includedirs.push_back(s);
      }
    } else if (string(*args).substr(0,2) == "-L") {
      string s = string(*args).substr(2);
      if (s.empty()) {
	if (!*++args) {
	  interp.error(prog + ": -L lacks directory argument");
	  return 1;
	}
	s = *args;
      }
      s = unixize(s);
      if (!s.empty()) {
	if (s[s.size()-1] != '/') s.append("/");
	interp.librarydirs.push_back(s);
      }
    } else if (string(*args).substr(0,2) == "-v") {
      string s = string(*args).substr(2);
      if (s.empty()) continue;
      char *end;
      strtoul(s.c_str(), &end, 0);
      if (*end) {
	interp.error(prog + ": invalid option " + *args);
	return 1;
      }
    } else if (*args == string("-x")) {
      while (*++args) myargs.push_back(*args);
      break;
    } else if (*args == string("--")) {
      while (*++args) myargs.push_back(*args);
      break;
    } else if (**args == '-') {
      interp.error(prog + ": invalid option " + *args);
      return 1;
    }
  if ((env = getenv("PURE_INCLUDE")))
    add_path(interp.includedirs, unixize(env));
  if ((env = getenv("PURE_LIBRARY")))
    add_path(interp.librarydirs, unixize(env));
  interp.init_sys_vars(PACKAGE_VERSION, HOST, myargs);
  if (want_prelude) {
    // load the prelude if we can find it
    if (chkfile(prelude)) {
      have_prelude = true;
      interp.run(prelude, false);
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
    } else if (*argv == string("-x")) {
      if (*++argv) {
	count++; interp.modname = *argv;
	interp.run(*argv, false);
      } else {
	interp.error(prog + ": missing script name");
	return 1;
      }
      break;
    } else if (*argv == string("--"))
      break;
    else if (string(*argv).substr(0,2) == "-I" ||
	     string(*argv).substr(0,2) == "-L") {
      string s = string(*argv).substr(2);
      if (s.empty()) ++argv;
    } else if (**argv == '-')
      ;
    else if (**argv) {
      if (count++ == 0) interp.modname = *argv;
      interp.run(*argv, false);
    }
  if (count > 0 && !force_interactive) {
    if (interp.verbose&verbosity::dump) interp.compile();
    return 0;
  }
  interp.symtab.init_builtins();
  // enter the interactive command loop
  interp.interactive = true;
  if (isatty(fileno(stdin)) || force_interactive) {
    // We're connected to a terminal (or pretend that we are), print the
    // sign-on message.
    if (!quiet) {
      cout << "Pure " << PACKAGE_VERSION << " (" << HOST << ") "
	   << COPYRIGHT << endl << LICENSE;
      if (have_prelude)
	cout << "Loaded prelude from " << prelude << ".\n\n";
    }
    interp.compile();
    interp.ttymode = true;
  }
  if (isatty(fileno(stdin))) {
    // initialize readline
    extern bool using_readline;
    using_readline = true;
    rl_readline_name = "Pure";
    rl_attempted_completion_function = pure_completion;
    using_history();
    read_history(interp.histfile.c_str());
    stifle_history(600);
    histfile = strdup(interp.histfile.c_str());
  }
  interp.temp = 1;
  interp.run("", false);
  if (interp.ttymode) cout << endl;
  return 0;
}
