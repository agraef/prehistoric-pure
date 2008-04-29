
#include "symtable.hh"
#include <assert.h>

symtable::symtable() : fno(0), rtab(1024)
{
  // enter any predefined symbols here, e.g.:
  //sym("-", 6, infixl);
}

void symtable::init_builtins()
{
  nil_sym();
  cons_sym();
  void_sym();
  pair_sym();
  not_sym();
  bitnot_sym();
  or_sym();
  and_sym();
  bitor_sym();
  bitand_sym();
  shl_sym();
  shr_sym();
  less_sym();
  greater_sym();
  lesseq_sym();
  greatereq_sym();
  equal_sym();
  notequal_sym();
  plus_sym();
  minus_sym();
  mult_sym();
  fdiv_sym();
  div_sym();
  mod_sym();
  catch_sym();
  catmap_sym();
  failed_match_sym();
  failed_cond_sym();
  signal_sym();
  segfault_sym();
}

symbol* symtable::lookup(const string& s)
{
  map<string, symbol>::iterator it = tab.find(s);
  if (it == tab.end())
    return 0;
  else
    return &it->second;
}

symbol& symtable::sym(const string& s)
{
  symbol& _sym = tab[s];
  if (_sym.f == 0) {
    if ((uint32_t)++fno > rtab.capacity())
      rtab.reserve(rtab.capacity()+1024);
    _sym = symbol(s, fno);
    //cout << "new symbol " << _sym.f << ": " << _sym.s << endl;
    rtab[fno] = &_sym;
  }
  return _sym;
}

symbol& symtable::sym(const string& s, prec_t prec, fix_t fix)
{
  assert(prec <= 10);
  symbol& _sym = tab[s];
  if (_sym.f == 0) {
    if ((uint32_t)++fno > rtab.capacity())
      rtab.reserve(rtab.capacity()+1024);
    _sym = symbol(s, fno, prec, fix);
    //cout << "new symbol " << _sym.f << ": " << _sym.s << endl;
    rtab[fno] = &_sym;
  }
  return _sym;
}

symbol& symtable::sym(int32_t f)
{
  assert(f > 0 && (uint32_t)f < rtab.size());
  return *rtab[f];
}

symbol& symtable::nil_sym()
{
  symbol *_sym = lookup("[]");
  if (_sym)
    return *_sym;
  else
    return sym("[]", 10, nullary);
}

symbol& symtable::cons_sym()
{
  symbol *_sym = lookup(":");
  if (_sym)
    return *_sym;
  else
    return sym(":", 4, infixr);
}

symbol& symtable::void_sym()
{
  symbol *_sym = lookup("()");
  if (_sym)
    return *_sym;
  else
    return sym("()", 10, nullary);
}

symbol& symtable::pair_sym()
{
  symbol *_sym = lookup(",");
  if (_sym)
    return *_sym;
  else
    return sym(",", 1, infixr);
}

symbol& symtable::not_sym()
{
  symbol *_sym = lookup("not");
  if (_sym)
    return *_sym;
  else
    return sym("not", 3, prefix);
}

symbol& symtable::bitnot_sym()
{
  symbol *_sym = lookup("~");
  if (_sym)
    return *_sym;
  else
    return sym("~", 7, prefix);
}

symbol& symtable::or_sym()
{
  symbol *_sym = lookup("||");
  if (_sym)
    return *_sym;
  else
    return sym("||", 2, infixr);
}

symbol& symtable::and_sym()
{
  symbol *_sym = lookup("&&");
  if (_sym)
    return *_sym;
  else
    return sym("&&", 3, infixr);
}

symbol& symtable::bitor_sym()
{
  symbol *_sym = lookup("|");
  if (_sym)
    return *_sym;
  else
    return sym("|", 6, infixl);
}

symbol& symtable::bitand_sym()
{
  symbol *_sym = lookup("&");
  if (_sym)
    return *_sym;
  else
    return sym("&", 7, infixl);
}

symbol& symtable::shl_sym()
{
  symbol *_sym = lookup("<<");
  if (_sym)
    return *_sym;
  else
    return sym("<<", 5, infixl);
}

symbol& symtable::shr_sym()
{
  symbol *_sym = lookup(">>");
  if (_sym)
    return *_sym;
  else
    return sym(">>", 5, infixl);
}

symbol& symtable::less_sym()
{
  symbol *_sym = lookup("<");
  if (_sym)
    return *_sym;
  else
    return sym("<", 4, infix);
}

symbol& symtable::greater_sym()
{
  symbol *_sym = lookup(">");
  if (_sym)
    return *_sym;
  else
    return sym(">", 4, infix);
}

symbol& symtable::lesseq_sym()
{
  symbol *_sym = lookup("<=");
  if (_sym)
    return *_sym;
  else
    return sym("<=", 4, infix);
}

symbol& symtable::greatereq_sym()
{
  symbol *_sym = lookup(">=");
  if (_sym)
    return *_sym;
  else
    return sym(">=", 4, infix);
}

symbol& symtable::equal_sym()
{
  symbol *_sym = lookup("==");
  if (_sym)
    return *_sym;
  else
    return sym("==", 4, infix);
}

symbol& symtable::notequal_sym()
{
  symbol *_sym = lookup("!=");
  if (_sym)
    return *_sym;
  else
    return sym("!=", 4, infix);
}

symbol& symtable::plus_sym()
{
  symbol *_sym = lookup("+");
  if (_sym)
    return *_sym;
  else
    return sym("+", 6, infixl);
}

symbol& symtable::minus_sym()
{
  symbol *_sym = lookup("-");
  if (_sym)
    return *_sym;
  else
    return sym("-", 6, infixl);
}

symbol& symtable::mult_sym()
{
  symbol *_sym = lookup("*");
  if (_sym)
    return *_sym;
  else
    return sym("*", 7, infixl);
}

symbol& symtable::fdiv_sym()
{
  symbol *_sym = lookup("/");
  if (_sym)
    return *_sym;
  else
    return sym("/", 7, infixl);
}

symbol& symtable::div_sym()
{
  symbol *_sym = lookup("div");
  if (_sym)
    return *_sym;
  else
    return sym("div", 7, infixl);
}

symbol& symtable::mod_sym()
{
  symbol *_sym = lookup("mod");
  if (_sym)
    return *_sym;
  else
    return sym("mod", 7, infixl);
}
