
#ifndef SYMTABLE_HH
#define SYMTABLE_HH

#include <string>
#include <map>
#include <vector>
#include <stdint.h>
#include "expr.hh"
#include "printer.hh"

using namespace std;

/* Symbol table entries. */

class symbol {
public:
  expr x; // cached expression node
  int32_t f; // symbol tag
  string s; // print name
  prec_t prec; // precedence level
  fix_t fix; // fixity
  symbol() : // constructor for dummy entries
    f(0), s(""), prec(10), fix(infix) { }
  symbol(const string& _s, int _f) :
    f(_f), s(_s), prec(10), fix(infix) { x = expr(f); }
  symbol(const string& _s, int _f, prec_t _prec, fix_t _fix) :
    f(_f), s(_s), prec(_prec), fix(_fix) { x = expr(f); }
};

/* Symbol table. */

class symtable {
  int32_t fno;
public:
  map<string, symbol> tab;
  vector<symbol*> rtab;
  symtable();
  // add default declarations for the builtin constants and operators (to be
  // invoked *after* possibly reading the prelude)
  void init_builtins();
  // look up an existing symbol (return 0 if not in table)
  symbol* lookup(const string& s);
  // get a symbol by its name (create if necessary)
  symbol& sym(const string& s);
  symbol& sym(const string& s, prec_t prec, fix_t fix);
  // retrieve various builtin symbols (create when necessary)
  symbol& nil_sym();
  symbol& cons_sym();
  symbol& void_sym();
  symbol& pair_sym();
  symbol& seq_sym();
  symbol& neg_sym() { return sym("neg"); }
  symbol& not_sym();
  symbol& bitnot_sym();
  symbol& or_sym();
  symbol& and_sym();
  symbol& bitor_sym();
  symbol& bitand_sym();
  symbol& shl_sym();
  symbol& shr_sym();
  symbol& less_sym();
  symbol& greater_sym();
  symbol& lesseq_sym();
  symbol& greatereq_sym();
  symbol& equal_sym();
  symbol& notequal_sym();
  symbol& plus_sym();
  symbol& minus_sym();
  symbol& mult_sym();
  symbol& fdiv_sym();
  symbol& div_sym();
  symbol& mod_sym();
  symbol& catch_sym() { return sym("catch"); }
  symbol& catmap_sym() { return sym("catmap"); }
  symbol& failed_match_sym() { return sym("failed_match"); }
  symbol& failed_cond_sym() { return sym("failed_cond"); }
  symbol& signal_sym() { return sym("signal"); }
  symbol& segfault_sym() { return sym("stack_fault"); }
  // get a symbol by its number
  symbol& sym(int32_t f);
};

#endif // ! SYMTABLE_HH
