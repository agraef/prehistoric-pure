
/* AIX requires this to be the first thing in the file.  */
#ifndef __GNUC__
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
#pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
char *alloca ();
#   endif
#  endif
# endif
#endif
#ifdef __MINGW32__
#include <malloc.h>
#endif

#include "runtime.h"
#include "expr.hh"
#include "interpreter.hh"
#include "util.hh"
#include <readline/readline.h>
#include <readline/history.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>
#include <iostream>
#include <sstream>

#include "config.h"
#include "funcall.h"

#ifdef HAVE_GSL
#include <gsl/gsl_errno.h>
#include <gsl/gsl_matrix.h>
#endif

// Hooks to report stack overflows and other kinds of hard errors.
#define checkstk(test) if (interpreter::stackmax > 0 &&			\
      interpreter::stackdir*(&test - interpreter::baseptr)		\
      >= interpreter::stackmax)						\
    pure_throw(stack_exception())
#define checkall(test) if (interpreter::stackmax > 0 &&			\
      interpreter::stackdir*(&test - interpreter::baseptr)		\
      >= interpreter::stackmax) {					\
    interpreter::brkmask = 0;						\
    pure_throw(stack_exception());					\
  } else if (interpreter::brkmask)					\
    interpreter::brkmask = 0;						\
  else if (interpreter::brkflag)					\
    pure_throw(signal_exception(interpreter::brkflag))

// Debug expression allocations. Warns about expression memory leaks.
// NOTE: Bookkeeping starts and ends at each toplevel pure_invoke call.
// Enabling this code will make the interpreter *much* slower.
#if MEMDEBUG
#if MEMDEBUG>1 // enable this to print each and every expression (de)allocation
#define MEMDEBUG_NEW(x)  interpreter::g_interp->mem_allocations.insert(x); \
  cerr << "NEW:  " << (void*)x << ": " << x << endl;
#define MEMDEBUG_FREE(x) interpreter::g_interp->mem_allocations.erase(x); \
  cerr << "FREE: " << (void*)x << ": " << x << endl;
#else
#define MEMDEBUG_NEW(x)  interpreter::g_interp->mem_allocations.insert(x);
#define MEMDEBUG_FREE(x) interpreter::g_interp->mem_allocations.erase(x);
#endif
#define MEMDEBUG_INIT if (interpreter::g_interp->estk.empty())	\
    interpreter::g_interp->mem_allocations.clear();
#define MEMDEBUG_SUMMARY(ret) if (interpreter::g_interp->estk.empty()) {\
    mem_mark(ret);							\
    if (!interpreter::g_interp->mem_allocations.empty()) {		\
      cerr << "** WARNING: leaked expressions:\n";			\
      for (set<pure_expr*>::iterator x =				\
	     interpreter::g_interp->mem_allocations.begin();		\
	   x != interpreter::g_interp->mem_allocations.end(); x++)	\
	cerr << (void*)(*x) << " (refc = " << (*x)->refc << "): "	\
	     << (*x) << endl;						\
      interpreter::g_interp->mem_allocations.clear();			\
    }									\
  }
static void mem_mark(pure_expr *x)
{
  interpreter::g_interp->mem_allocations.erase(x);
  if (x->tag == EXPR::APP) {
    mem_mark(x->data.x[0]);
    mem_mark(x->data.x[1]);
  } else if (x->tag >= 0 && x->data.clos) {
    for (size_t i = 0; i < x->data.clos->m; i++)
      mem_mark(x->data.clos->env[i]);
  }
}
#else
#define MEMDEBUG_NEW(x)
#define MEMDEBUG_FREE(x)
#define MEMDEBUG_INIT
#define MEMDEBUG_SUMMARY(ret)
#endif

// Debug shadow stack manipulations. Prints pushes and pops of stack frames.
// NOTE: Enabling this code generates *lots* of debugging output.
#if DEBUG>2
#define SSTK_DEBUG 1
#else
#define SSTK_DEBUG 0
#endif

static inline pure_expr* pure_apply2(pure_expr *x, pure_expr *y)
{
  // Count references and construct a function application.
  pure_new_args(2, x, y);
  return pure_apply(x, y);
}

static inline pure_expr* signal_exception(int sig)
{
  if (!interpreter::g_interp) return 0;
  pure_expr *f = pure_const(interpreter::g_interp->symtab.signal_sym().f);
  pure_expr *x = pure_int(sig);
  return pure_apply2(f, x);
}

static inline pure_expr* stack_exception()
{
  if (!interpreter::g_interp) return 0;
  return pure_const(interpreter::g_interp->symtab.segfault_sym().f);
}

static inline pure_expr* not_implemented_exception()
{
  if (!interpreter::g_interp) return 0;
  return pure_const(interpreter::g_interp->symtab.not_implemented_sym().f);
}

static inline pure_expr *get_sentry(pure_expr *x)
{
  if (x==0)
    return 0;
  else if (x->tag == EXPR::APP || x->tag == EXPR::PTR)
    return x->data.x[2];
  else
    return 0;
}

static inline void call_sentry(pure_expr *x)
{
  if (x->tag == EXPR::APP || x->tag == EXPR::PTR) {
    pure_expr *s = x->data.x[2];
    if (s) {
      ++x->refc;
      pure_freenew(pure_apply2(s, x));
      --x->refc;
    }
  }
}

static inline void free_sentry(pure_expr *x)
{
  if (x->tag == EXPR::APP || x->tag == EXPR::PTR) {
    pure_expr *s = x->data.x[2];
    if (s) pure_free(s);
  }
}

// Expression pointers are allocated in larger chunks for better performance.
// NOTE: Only internal fields get initialized by new_expr(), the remaining
// fields *must* be initialized as appropriate by the caller.

static inline pure_expr *new_expr()
{
  interpreter& interp = *interpreter::g_interp;
  pure_expr *x = interp.exps;
  if (x)
    interp.exps = x->xp;
  else if (interp.mem && interp.mem->p-interp.mem->x < MEMSIZE)
    x = interp.mem->p++;
  else {
    pure_mem *mem = interp.mem;
    interp.mem = new pure_mem;
    interp.mem->next = mem;
    interp.mem->p = interp.mem->x;
    x = interp.mem->p++;
  }
  x->refc = 0;
  x->xp = interp.tmps;
  x->data.x[2] = 0; // initialize the sentry
  interp.tmps = x;
  return x;
}

static inline void free_expr(pure_expr *x)
{
  interpreter& interp = *interpreter::g_interp;
  x->xp = interp.exps;
  interp.exps = x;
  MEMDEBUG_FREE(x)
}

static inline
pure_expr *pure_new_internal(pure_expr *x)
{
  interpreter& interp = *interpreter::g_interp;
  assert(x && "pure_new: null expression");
  assert((x->refc==0 || !x->xp) && "pure_new: corrupt expression data");
#if DEBUG>2
  if (x->tag >= 0 && x->data.clos)
    cerr << "pure_new: " << (x->data.clos->local?"local":"global")
	 << " closure " << x << " (" << (void*)x << "), refc = "
	 << x->refc << endl;
#endif
  if (x->refc++ == 0) {
    // remove x from the list of temporaries
    if (interp.tmps == x)
      interp.tmps = x->xp;
    else {
      // walk the list to find the place where x has to be unlinked
      pure_expr *tmps = interp.tmps;
      while (tmps && tmps->xp != x) tmps = tmps->xp;
      assert(tmps);
      tmps->xp = x->xp;
    }
    x->xp = 0;
  }
  return x;
}

static void pure_free_clos(pure_expr *x)
{
#if DEBUG>3
  cerr << "pure_free, freeing environment: "
       << (x->data.clos->local?"local":"global") << " closure "
       << x << " (" << (void*)x << "), refc = " << x->refc << endl;
#endif
  if (x->data.clos->ep) {
    Env *env = (Env*)x->data.clos->ep;
    assert(env->refc > 0);
    if (--env->refc == 0) delete env;
  }
  if (x->data.clos->env) {
    for (size_t i = 0; i < x->data.clos->m; i++)
      pure_free(x->data.clos->env[i]);
    delete[] x->data.clos->env;
  }
  delete x->data.clos;
}

static pure_closure *pure_copy_clos(pure_closure *clos)
{
  assert(clos);
  pure_closure *ret = new pure_closure;
  ret->local = clos->local;
  ret->thunked = clos->thunked;
  ret->n = clos->n;
  ret->m = clos->m;
  ret->fp = clos->fp;
  ret->ep = clos->ep;
  if (clos->ep) ((Env*)clos->ep)->refc++;
  if (clos->m == 0)
    ret->env = 0;
  else {
    ret->env = new pure_expr*[clos->m];
    for (size_t i = 0; i < clos->m; i++) {
      ret->env[i] = clos->env[i];
      assert(clos->env[i]->refc > 0);
      clos->env[i]->refc++;
    }
  }
  return ret;
}

static void pure_free_matrix(pure_expr *x)
{
  if (!x->data.mat.p) return;
  assert(x->data.mat.refc && "pure_free_matrix: corrupt data");
  assert(*x->data.mat.refc > 0 && "pure_free_matrix: unreferenced data");
  bool owner = --*x->data.mat.refc == 0;
#ifdef HAVE_GSL
  switch (x->tag) {
  case EXPR::MATRIX: {
    gsl_matrix *m = (gsl_matrix*)x->data.mat.p;
    m->owner = owner;
    gsl_matrix_free(m);
    break;
  }
  case EXPR::CMATRIX: {
    gsl_matrix_complex *m = (gsl_matrix_complex*)x->data.mat.p;
    m->owner = owner;
    gsl_matrix_complex_free(m);
    break;
  }
  case EXPR::IMATRIX: {
    gsl_matrix_int *m = (gsl_matrix_int*)x->data.mat.p;
    m->owner = owner;
    gsl_matrix_int_free(m);
    break;
  }
  default:
    assert(0 && "pure_free_matrix: corrupt data");
    break;
  }
#endif
  if (owner) delete x->data.mat.refc;
}

#if 1

/* This is implemented (mostly) non-recursively to prevent stack overflows,
   using the xp field to form a stack on the fly. */

static inline
void pure_free_internal(pure_expr *x)
{
  assert(x && "pure_free: null expression");
  assert(x->refc > 0 && "pure_free: unreferenced expression");
  assert(!x->xp && "pure_free: corrupt expression data");
  pure_expr *xp = 0, *y;
 loop:
  if (--x->refc == 0) {
    call_sentry(x);
    switch (x->tag) {
    case EXPR::APP:
      y = x->data.x[0];
      assert(!x->xp);
      x->xp = xp; xp = x; x = y;
      goto loop;
    case EXPR::INT:
    case EXPR::DBL:
    case EXPR::PTR:
      // nothing to do
      break;
    case EXPR::BIGINT:
      mpz_clear(x->data.z);
      break;
    case EXPR::STR:
      free(x->data.s);
      break;
    case EXPR::MATRIX:
    case EXPR::CMATRIX:
    case EXPR::IMATRIX:
      pure_free_matrix(x);
      break;
    default:
      assert(x->tag >= 0);
      if (x->data.clos) pure_free_clos(x);
      break;
    }
  }
  while (xp && x == xp->data.x[1]) {
    if (x->refc == 0) {
      free_sentry(x);
      free_expr(x);
    }
    x = xp; xp = x->xp;
  }
  if (x->refc == 0) {
    free_sentry(x);
    free_expr(x);
  }
  if (xp) {
    x = xp->data.x[1];
    goto loop;
  }
}

#else

/* This version is only included here for reference and debugging purposes,
   normal builds should use the non-recursive version above. */

static
void pure_free_internal(pure_expr *x)
{
  if (--x->refc == 0) {
    call_sentry(x);
    switch (x->tag) {
    case EXPR::APP:
      pure_free_internal(x->data.x[0]);
      pure_free_internal(x->data.x[1]);
      break;
    case EXPR::INT:
    case EXPR::DBL:
      break;
    case EXPR::BIGINT:
      mpz_clear(x->data.z);
      break;
    case EXPR::STR:
      free(x->data.s);
      break;
    case EXPR::PTR:
      break;
    default:
      assert(x->tag >= 0);
      if (x->data.clos) pure_free_clos(x);
      break;
    }
    free_sentry(x);
    free_expr(x);
  }
}

#endif

static void inline
pure_unref_internal(pure_expr *x)
{
  assert(x && "pure_unref: null expression");
  assert(x->refc > 0 && "pure_unref: unreferenced expression");
  if (--x->refc == 0 && !x->xp) {
    interpreter& interp = *interpreter::g_interp;
    // check whether x is already on the tmps list
    pure_expr *tmps = interp.tmps;
    while (tmps && tmps != x) tmps = tmps->xp;
    if (!tmps) {
      // put x on the tmps list again
      x->xp = interp.tmps;
      interp.tmps = x;
    }
  }
}

/* PUBLIC API. **************************************************************/

extern "C"
int32_t pure_sym(const char *s)
{
  assert(s);
  interpreter& interp = *interpreter::g_interp;
  const symbol& sym = interp.symtab.sym(s);
  return sym.f;
}

extern "C"
int32_t pure_getsym(const char *s)
{
  assert(s);
  interpreter& interp = *interpreter::g_interp;
  const symbol* sym = interp.symtab.lookup(s);
  if (sym)
    return sym->f;
  else
    return 0;
}

extern "C"
const char *pure_sym_pname(int32_t tag)
{
  assert(tag>0);
  interpreter& interp = *interpreter::g_interp;
  const symbol& sym = interp.symtab.sym(tag);
  return sym.s.c_str();
}

extern "C"
int8_t pure_sym_nprec(int32_t tag)
{
  assert(tag>0);
  interpreter& interp = *interpreter::g_interp;
  const symbol& sym = interp.symtab.sym(tag);
  return nprec(sym.prec, sym.fix);
}

extern "C"
pure_expr *pure_symbol(int32_t tag)
{
  assert(tag>0);
  interpreter& interp = *interpreter::g_interp;
  const symbol& sym = interp.symtab.sym(tag);
  // Check for an existing global variable for this symbol.
  GlobalVar& v = interp.globalvars[tag];
  if (!v.v) {
    // The variable doesn't exist yet (we have a new symbol), create it.
    string lab;
    // Create a name for the variable (cf. interpreter::mkvarlabel).
    if (sym.prec < 10 || sym.fix == nullary)
      lab = "$("+sym.s+")";
    else
      lab = "$"+sym.s;
    // Create a global variable bound to the symbol for now.
    v.v = new llvm::GlobalVariable
      (interp.ExprPtrTy, false, llvm::GlobalVariable::InternalLinkage, 0,
       lab.c_str(), interp.module);
    interp.JIT->addGlobalMapping(v.v, &v.x);
    v.x = pure_new(pure_const(tag));
    // Since we just created this variable, it doesn't have any closure bound
    // to it yet, so it's safe to just return the symbol as is.
    return v.x;
  } else
    // The symbol already exists, so there might be a parameterless closure
    // bound to it and thus we need to evaluate it.
    return pure_call(v.x);
}

extern "C"
pure_expr *pure_int(int32_t i)
{
  pure_expr *x = new_expr();
  x->tag = EXPR::INT;
  x->data.i = i;
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_mpz(const mpz_t z)
{
  pure_expr *x = new_expr();
  x->tag = EXPR::BIGINT;
  mpz_init_set(x->data.z, z);
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_double(double d)
{
  pure_expr *x = new_expr();
  x->tag = EXPR::DBL;
  x->data.d = d;
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_pointer(void *p)
{
  pure_expr *x = new_expr();
  x->tag = EXPR::PTR;
  x->data.p = p;
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_expr_pointer(void)
{
  pure_expr **p = (pure_expr**)malloc(sizeof(pure_expr*));
  if (p) {
    *p = 0;
    return pure_pointer(p);
  } else
    return 0;
}

extern "C"
pure_expr *pure_string_dup(const char *s)
{
  if (!s) return pure_pointer(0);
  pure_expr *x = new_expr();
  x->tag = EXPR::STR;
  x->data.s = strdup(s);
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_cstring_dup(const char *s)
{
  if (!s) return pure_pointer(0);
  pure_expr *x = new_expr();
  x->tag = EXPR::STR;
  x->data.s = toutf8(s, 0);
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_string(char *s)
{
  if (!s) return pure_pointer(0);
  pure_expr *x = new_expr();
  x->tag = EXPR::STR;
  x->data.s = s;
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_cstring(char *s)
{
  if (!s) return pure_pointer(0);
  pure_expr *x = pure_cstring_dup(s);
  free(s);
  return x;
}

extern "C"
pure_expr *pure_double_matrix(void *p)
{
#ifdef HAVE_GSL
  gsl_matrix *m = (gsl_matrix*)p;
  if (!m || !m->owner) return 0;
  pure_expr *x = new_expr();
  x->tag = EXPR::MATRIX;
  x->data.mat.p = p;
  x->data.mat.refc = new uint32_t;
  *x->data.mat.refc = 1;
  MEMDEBUG_NEW(x)
  return x;
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_complex_matrix(void *p)
{
#ifdef HAVE_GSL
  gsl_matrix_complex *m = (gsl_matrix_complex*)p;
  if (!m || !m->owner) return 0;
  pure_expr *x = new_expr();
  x->tag = EXPR::CMATRIX;
  x->data.mat.p = p;
  x->data.mat.refc = new uint32_t;
  *x->data.mat.refc = 1;
  MEMDEBUG_NEW(x)
  return x;
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_int_matrix(void *p)
{
#ifdef HAVE_GSL
  gsl_matrix_int *m = (gsl_matrix_int*)p;
  if (!m || !m->owner) return 0;
  pure_expr *x = new_expr();
  x->tag = EXPR::IMATRIX;
  x->data.mat.p = p;
  x->data.mat.refc = new uint32_t;
  *x->data.mat.refc = 1;
  MEMDEBUG_NEW(x)
  return x;
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_double_matrix_dup(const void *p)
{
#ifdef HAVE_GSL
  gsl_matrix *m1 = (gsl_matrix*)p;
  if (!m1) return 0;
  gsl_matrix *m2 = gsl_matrix_alloc(m1->size1, m1->size2);
  gsl_matrix_memcpy(m2, m1);
  return pure_double_matrix(m2);
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_complex_matrix_dup(const void *p)
{
#ifdef HAVE_GSL
  gsl_matrix_complex *m1 = (gsl_matrix_complex*)p;
  if (!m1) return 0;
  gsl_matrix_complex *m2 = gsl_matrix_complex_alloc(m1->size1, m1->size2);
  gsl_matrix_complex_memcpy(m2, m1);
  return pure_complex_matrix(m2);
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_int_matrix_dup(const void *p)
{
#ifdef HAVE_GSL
  gsl_matrix_int *m1 = (gsl_matrix_int*)p;
  if (!m1) return 0;
  gsl_matrix_int *m2 = gsl_matrix_int_alloc(m1->size1, m1->size2);
  gsl_matrix_int_memcpy(m2, m1);
  return pure_int_matrix(m2);
#else
  return 0;
#endif
}

#ifdef HAVE_GSL
static pure_expr*
double_matrix_rows(size_t nrows, size_t ncols, size_t n, pure_expr **xs)
{
  gsl_matrix *mat = gsl_matrix_alloc(nrows, ncols);
  if (!mat) return 0; // XXXTODO: empty matrices
  double *data = mat->data;
  size_t tda = mat->tda;
  for (size_t count = 0, i = 0; count < n; count++) {
    pure_expr *x = xs[count];
    switch (x->tag) {
    case EXPR::INT:
      data[i++*tda] = (double)x->data.i;
      break;
    case EXPR::BIGINT:
      data[i++*tda] = mpz_get_d(x->data.z);
      break;
    case EXPR::DBL:
      data[i++*tda] = x->data.d;
      break;
    case EXPR::MATRIX: {
      gsl_matrix *mat1 = (gsl_matrix*)x->data.mat.p;
      if (mat1)
	for (size_t j = 0; j < mat1->size1; i++, j++)
	  memcpy(data+i*tda, mat1->data+j*mat1->tda, ncols*sizeof(double));
      break;
    }
    case EXPR::IMATRIX: {
      gsl_matrix_int *mat1 = (gsl_matrix_int*)x->data.mat.p;
      if (mat1)
	for (size_t j = 0; j < mat1->size1; i++, j++)
	  for (size_t k = 0; k < mat1->size2; k++)
	    data[i*tda+k] = (double)mat1->data[j*mat1->tda+k];
      break;
    }
    default:
      assert(0 && "bad matrix element");
      break;
    }
    pure_freenew(x);
  }
  return pure_double_matrix(mat);
}

static pure_expr*
double_matrix_columns(size_t nrows, size_t ncols, size_t n, pure_expr **xs)
{
  gsl_matrix *mat = gsl_matrix_alloc(nrows, ncols);
  if (!mat) return 0; // XXXTODO: empty matrices
  double *data = mat->data;
  size_t tda = mat->tda;
  for (size_t count = 0, i = 0; count < n; count++) {
    pure_expr *x = xs[count];
    switch (x->tag) {
    case EXPR::INT:
      data[i++] = (double)x->data.i;
      break;
    case EXPR::BIGINT:
      data[i++] = mpz_get_d(x->data.z);
      break;
    case EXPR::DBL:
      data[i++] = x->data.d;
      break;
    case EXPR::MATRIX: {
      gsl_matrix *mat1 = (gsl_matrix*)x->data.mat.p;
      if (mat1)
	for (size_t j = 0; j < mat1->size1; i++, j++)
	  memcpy(data+j*tda+i, mat1->data+j*mat1->tda, ncols*sizeof(double));
      i += mat1->size2;
      break;
    }
    case EXPR::IMATRIX: {
      gsl_matrix_int *mat1 = (gsl_matrix_int*)x->data.mat.p;
      if (mat1)
	for (size_t j = 0; j < mat1->size1; j++)
	  for (size_t k = 0; k < mat1->size2; k++)
	    data[j*tda+k+i] = (double)mat1->data[j*mat1->tda+k];
      i += mat1->size2;
      break;
    }
    default:
      assert(0 && "bad matrix element");
      break;
    }
    pure_freenew(x);
  }
  return pure_double_matrix(mat);
}

static pure_expr*
int_matrix_rows(size_t nrows, size_t ncols, size_t n, pure_expr **xs)
{
  gsl_matrix_int *mat = gsl_matrix_int_alloc(nrows, ncols);
  if (!mat) return 0; // XXXTODO: empty matrices
  int *data = mat->data;
  size_t tda = mat->tda;
  for (size_t count = 0, i = 0; count < n; count++) {
    pure_expr *x = xs[count];
    switch (x->tag) {
    case EXPR::INT:
      data[i++*tda] = x->data.i;
      break;
    case EXPR::BIGINT:
      data[i++*tda] = pure_get_int(x);
      break;
    case EXPR::DBL:
      data[i++*tda] = (int)x->data.d;
      break;
    case EXPR::MATRIX: {
      gsl_matrix_int *mat1 = (gsl_matrix_int*)x->data.mat.p;
      if (mat1)
	for (size_t j = 0; j < mat1->size1; i++, j++)
	  for (size_t k = 0; k < mat1->size2; k++)
	    data[i*tda+k] = (int)mat1->data[j*mat1->tda+k];
      break;
    }
    case EXPR::IMATRIX: {
      gsl_matrix *mat1 = (gsl_matrix*)x->data.mat.p;
      if (mat1)
	for (size_t j = 0; j < mat1->size1; i++, j++)
	  memcpy(data+i*tda, mat1->data+j*mat1->tda, ncols*sizeof(int));
      break;
    }
    default:
      assert(0 && "bad matrix element");
      break;
    }
    pure_freenew(x);
  }
  return pure_int_matrix(mat);
}

static pure_expr*
int_matrix_columns(size_t nrows, size_t ncols, size_t n, pure_expr **xs)
{
  gsl_matrix_int *mat = gsl_matrix_int_alloc(nrows, ncols);
  if (!mat) return 0; // XXXTODO: empty matrices
  int *data = mat->data;
  size_t tda = mat->tda;
  for (size_t count = 0, i = 0; count < n; count++) {
    pure_expr *x = xs[count];
    switch (x->tag) {
    case EXPR::INT:
      data[i++] = x->data.i;
      break;
    case EXPR::BIGINT:
      data[i++] = pure_get_int(x);
      break;
    case EXPR::DBL:
      data[i++] = (int)x->data.d;
      break;
    case EXPR::MATRIX: {
      gsl_matrix_int *mat1 = (gsl_matrix_int*)x->data.mat.p;
      if (mat1)
	for (size_t j = 0; j < mat1->size1; j++)
	  for (size_t k = 0; k < mat1->size2; k++)
	    data[j*tda+k+i] = (int)mat1->data[j*mat1->tda+k];
      i += mat1->size2;
      break;
    }
    case EXPR::IMATRIX: {
      gsl_matrix *mat1 = (gsl_matrix*)x->data.mat.p;
      if (mat1)
	for (size_t j = 0; j < mat1->size1; i++, j++)
	  memcpy(data+j*tda+i, mat1->data+j*mat1->tda, ncols*sizeof(int));
      i += mat1->size2;
      break;
    }
    default:
      assert(0 && "bad matrix element");
      break;
    }
    pure_freenew(x);
  }
  return pure_int_matrix(mat);
}
#endif

extern "C"
pure_expr *pure_matrix_rowsl(uint32_t n, ...)
{
#ifdef HAVE_GSL
  // XXXTODO
  return 0;
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_matrix_rowsv(uint32_t n, pure_expr **elems)
{
#ifdef HAVE_GSL
  // XXXTODO
  return 0;
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_matrix_columnsl(uint32_t n, ...)
{
#ifdef HAVE_GSL
  // XXXTODO
  return 0;
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_matrix_columnsv(uint32_t n, pure_expr **elems)
{
#ifdef HAVE_GSL
  // XXXTODO
  return 0;
#else
  return 0;
#endif
}

extern "C"
pure_expr *pure_app(pure_expr *fun, pure_expr *arg)
{
  return pure_apply2(fun, arg);
}

extern "C"
pure_expr *pure_appl(pure_expr *fun, size_t argc, ...)
{
  if (argc == 0) return fun;
  va_list ap;
  va_start(ap, argc);
  pure_expr **args = (pure_expr**)alloca(argc*sizeof(pure_expr*));
  for (size_t i = 0; i < argc; i++)
    args[i] = va_arg(ap, pure_expr*);
  return pure_appv(fun, argc, args);
}

extern "C"
pure_expr *pure_appv(pure_expr *fun, size_t argc, pure_expr **args)
{
  pure_expr *y = fun;
  for (size_t i = 0; i < argc; i++)
    y = pure_apply2(y, args[i]);
  return y;
}

static inline pure_expr *mk_nil()
{
  interpreter& interp = *interpreter::g_interp;
  return pure_const(interp.symtab.nil_sym().f);
}

static inline pure_expr *mk_cons(pure_expr *x, pure_expr *y)
{
  interpreter& interp = *interpreter::g_interp;
  pure_expr *f = pure_const(interp.symtab.cons_sym().f);
  return pure_apply2(pure_apply2(f, x), y);
}

static inline pure_expr *mk_void()
{
  interpreter& interp = *interpreter::g_interp;
  return pure_const(interp.symtab.void_sym().f);
}

static inline pure_expr *mk_pair(pure_expr *x, pure_expr *y)
{
  interpreter& interp = *interpreter::g_interp;
  pure_expr *f = pure_const(interp.symtab.pair_sym().f);
  return pure_apply2(pure_apply2(f, x), y);
}

extern "C"
pure_expr *pure_listl(size_t size, ...)
{
  if (size == 0) return mk_nil();
  va_list ap;
  va_start(ap, size);
  pure_expr **elems = (pure_expr**)alloca(size*sizeof(pure_expr*));
  for (size_t i = 0; i < size; i++)
    elems[i] = va_arg(ap, pure_expr*);
  return pure_listv(size, elems);
}

extern "C"
pure_expr *pure_listv(size_t size, pure_expr **elems)
{
  pure_expr *y = mk_nil();
  for (size_t i = size; i-- > 0; )
    y = mk_cons(elems[i], y);
  return y;
}

extern "C"
pure_expr *pure_tuplel(size_t size, ...)
{
  if (size == 0) return mk_void();
  va_list ap;
  va_start(ap, size);
  pure_expr **elems = (pure_expr**)alloca(size*sizeof(pure_expr*));
  for (size_t i = 0; i < size; i++)
    elems[i] = va_arg(ap, pure_expr*);
  return pure_tuplev(size, elems);
}

extern "C"
pure_expr *pure_tuplev(size_t size, pure_expr **elems)
{
  if (size == 0) return mk_void();
  pure_expr *y = elems[--size];
  for (size_t i = size; i-- > 0; )
    y = mk_pair(elems[i], y);
  return y;
}

extern "C"
bool pure_is_symbol(const pure_expr *x, int32_t *sym)
{
  assert(x);
  if (x->tag >= 0) {
    if (sym) *sym = x->tag;
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_int(const pure_expr *x, int32_t *i)
{
  assert(x);
  if (x->tag == EXPR::INT) {
    if (i) *i = x->data.i;
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_mpz(const pure_expr *x, mpz_t *z)
{
  assert(x);
  if (x->tag == EXPR::BIGINT) {
    if (z) mpz_init_set(*z, x->data.z);
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_double(const pure_expr *x, double *d)
{
  assert(x);
  if (x->tag == EXPR::DBL) {
    if (d) *d = x->data.d;
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_pointer(const pure_expr *x, void **p)
{
  assert(x);
  if (x->tag == EXPR::PTR) {
    if (p) *p = x->data.p;
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_string(const pure_expr *x, const char **s)
{
  assert(x);
  if (x->tag == EXPR::STR) {
    if (s) *s = x->data.s;
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_string_dup(const pure_expr *x, char **s)
{
  assert(x);
  if (x->tag == EXPR::STR) {
    if (s) *s = strdup(x->data.s);
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_cstring_dup(const pure_expr *x, char **s)
{
  assert(x);
  if (x->tag == EXPR::STR) {
    if (s) *s = fromutf8(x->data.s);
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_double_matrix(const pure_expr *x, const void **p)
{
  if (x->tag == EXPR::MATRIX) {
    *p = x->data.mat.p;
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_complex_matrix(const pure_expr *x, const void **p)
{
  if (x->tag == EXPR::CMATRIX) {
    *p = x->data.mat.p;
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_int_matrix(const pure_expr *x, const void **p)
{
  if (x->tag == EXPR::IMATRIX) {
    *p = x->data.mat.p;
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_app(const pure_expr *x, pure_expr **fun, pure_expr **arg)
{
  assert(x);
  if (x->tag == EXPR::APP) {
    if (fun) *fun = x->data.x[0];
    if (arg) *arg = x->data.x[1];
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_appv(pure_expr *x, pure_expr **_fun,
		  size_t *_argc, pure_expr ***_args)
{
  assert(x);
  pure_expr *u = x, *y, *z;
  size_t argc = 0;
  while (pure_is_app(u, &y, &z)) {
    argc++;
    u = y;
  }
  if (_fun) *_fun = u;
  if (_argc) *_argc = argc;
  if (_args)
    if (argc > 0) {
      pure_expr **args = (pure_expr**)malloc(argc*sizeof(pure_expr*));
      assert(args);
      size_t i = argc;
      u = x;
      while (pure_is_app(u, &y, &z)) {
	args[--i] = z;
	u = y;
      }
      *_args = args;
    } else
      *_args = 0;
  return true;
}

static inline bool is_nil(pure_expr *x)
{
  interpreter& interp = *interpreter::g_interp;
  return x->tag == interp.symtab.nil_sym().f;
}

static inline bool is_cons(pure_expr *x, pure_expr*& y, pure_expr*& z)
{
  interpreter& interp = *interpreter::g_interp;
  if (x->tag == EXPR::APP && x->data.x[0]->tag == EXPR::APP &&
      x->data.x[0]->data.x[0]->tag == interp.symtab.cons_sym().f) {
    y = x->data.x[0]->data.x[1];
    z = x->data.x[1];
    return true;
  } else
    return false;
}

static inline bool is_void(pure_expr *x)
{
  interpreter& interp = *interpreter::g_interp;
  return x->tag == interp.symtab.void_sym().f;
}

static inline bool is_pair(pure_expr *x, pure_expr*& y, pure_expr*& z)
{
  interpreter& interp = *interpreter::g_interp;
  if (x->tag == EXPR::APP && x->data.x[0]->tag == EXPR::APP &&
      x->data.x[0]->data.x[0]->tag == interp.symtab.pair_sym().f) {
    y = x->data.x[0]->data.x[1];
    z = x->data.x[1];
    return true;
  } else
    return false;
}

extern "C"
bool pure_is_listv(pure_expr *x, size_t *_size, pure_expr ***_elems)
{
  assert(x);
  pure_expr *u = x, *y, *z;
  size_t size = 0;
  while (is_cons(u, y, z)) {
    size++;
    u = z;
  }
  if (!is_nil(u)) return false;
  if (_size) *_size = size;
  if (_elems)
    if (size > 0) {
      pure_expr **elems = (pure_expr**)malloc(size*sizeof(pure_expr*));
      assert(elems);
      size_t i = 0;
      u = x;
      while (is_cons(u, y, z)) {
	elems[i++] = y;
	u = z;
      }
      *_elems = elems;
    } else
      *_elems = 0;
  return true;
}

extern "C"
bool pure_is_tuplev(pure_expr *x, size_t *_size, pure_expr ***_elems)
{
  assert(x);
  /* FIXME: This implementation assumes that tuples are right-recursive. If we
     change the tuple implementation in the prelude then this code has to be
     adapted accordingly. */
  pure_expr *u = x, *y, *z;
  size_t size = 1;
  while (is_pair(u, y, z)) {
    size++;
    u = z;
  }
  if (_size) *_size = size;
  if (_elems) {
    pure_expr **elems = (pure_expr**)malloc(size*sizeof(pure_expr*));
    assert(elems);
    size_t i = 0;
    u = x;
    while (is_pair(u, y, z)) {
      elems[i++] = y;
      u = z;
    }
    elems[i++] = u;
    *_elems = elems;
  }
  return true;
}

extern "C"
pure_expr *pure_new(pure_expr *x)
{
  return pure_new_internal(x);
}

extern "C"
void pure_free(pure_expr *x)
{
  pure_free_internal(x);
}

extern "C"
void pure_freenew(pure_expr *x)
{
  if (x->refc == 0)
    pure_free_internal(pure_new_internal(x));
}

extern "C"
void pure_ref(pure_expr *x)
{
  x->refc++;
}

extern "C"
void pure_unref(pure_expr *x)
{
  pure_unref_internal(x);
}

extern "C"
pure_expr *pure_sentry(pure_expr *sentry, pure_expr *x)
{
  if (x==0)
    return 0;
  else if (x->tag == EXPR::APP || x->tag == EXPR::PTR) {
    if (x->data.x[2])
      pure_free_internal(x->data.x[2]);
    x->data.x[2] = sentry?pure_new_internal(sentry):0;
    return x;
  } else
    return 0;
}

extern "C"
pure_expr *pure_get_sentry(pure_expr *x)
{
  return get_sentry(x);
}

extern "C"
pure_expr *pure_clear_sentry(pure_expr *x)
{
  return pure_sentry(0, x);
}

extern "C"
bool pure_let(int32_t sym, pure_expr *x)
{
  if (sym <= 0 || !x) return false;
  try {
    interpreter& interp = *interpreter::g_interp;
    interp.defn(sym, x);
    return true;
  } catch (err &e) {
    return false;
  }
}

extern "C"
bool pure_def(int32_t sym, pure_expr *x)
{
  if (sym <= 0 || !x) return false;
  try {
    interpreter& interp = *interpreter::g_interp;
    interp.const_defn(sym, x);
    return true;
  } catch (err &e) {
    return false;
  }
}

extern "C"
bool pure_clear(int32_t sym)
{
  if (sym > 0) {
    interpreter& interp = *interpreter::g_interp;
    interp.clear();
    return true;
  } else
    return false;
}

extern "C"
uint8_t pure_save()
{
  interpreter& interp = *interpreter::g_interp;
  if (interp.temp < 0xff)
    return ++interp.temp;
  else
    return 0;
}

extern "C"
uint8_t pure_restore()
{
  interpreter& interp = *interpreter::g_interp;
  uint8_t level = interp.temp;
  interp.clear();
  if (level > 0 && interp.temp > level-1) --interp.temp;
  return interp.temp;
}

#ifndef HOST
#define HOST "unknown"
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.0"
#endif
#ifndef PURELIB
#define PURELIB "/usr/local/lib/pure-" PACKAGE_VERSION
#endif

#include <llvm/Target/TargetOptions.h>

#include <sys/types.h>
#include <sys/stat.h>

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

extern "C"
pure_interp *pure_create_interp(int argc, char *argv[])
{
  // This is pretty much the same as pure.cc:main(), except that some options
  // are ignored and there's no user interaction.
  char base;
  interpreter *_interp = new interpreter, &interp = *_interp;
  int count = 0;
  bool want_prelude = true, have_prelude = false;
  // This is used in advisory stack checks.
  if (!interpreter::baseptr) interpreter::baseptr = &base;
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
#if defined(HAVE_GSL) && DEBUG<2
  // Turn off GSL's own error handler which aborts the program.
  gsl_set_error_handler_off();
#endif
  // scan the command line options
  list<string> myargs;
  for (char **args = ++argv; *args; ++args)
    if (*args == string("-h") || *args == string("--help"))
      /* ignored */;
    else if (*args == string("--version"))
      /* ignored */;
    else if (*args == string("-i"))
      /* ignored */;
    else if (*args == string("-n") || *args == string("--noprelude"))
      want_prelude = false;
    else if (*args == string("--norc"))
      /* ignored */;
    else if (*args == string("--noediting"))
      /* ignored */;
    else if (*args == string("-q"))
      /* ignored */;
    else if (string(*args).substr(0,2) == "-I") {
      string s = string(*args).substr(2);
      if (s.empty()) {
	if (!*++args) {
	  cerr << "pure_create_interp: -I lacks directory argument\n";
	  delete _interp;
	  return 0;
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
	  cerr << "pure_create_interp: -L lacks directory argument\n";
	  delete _interp;
	  return 0;
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
	cerr << "pure_create_interp: invalid option " << *args << endl;
	delete _interp;
	return 0;
      }
    } else if (*args == string("-x")) {
      while (*++args) myargs.push_back(*args);
      break;
    } else if (*args == string("--")) {
      while (*++args) myargs.push_back(*args);
      break;
    } else if (**args == '-') {
      cerr << "pure_create_interp: invalid option " << *args << endl;
      delete _interp;
      return 0;
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
	cerr << "pure_create_interp: missing script name\n";
	delete _interp;
	return 0;
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
  interp.symtab.init_builtins();
  return (pure_interp*)_interp;
}

extern "C"
void pure_delete_interp(pure_interp *interp)
{
  assert(interp);
  interpreter *_interp = (interpreter*)interp;
  if (interpreter::g_interp == _interp)
    interpreter::g_interp = 0;
  delete _interp;
}

extern "C"
void pure_switch_interp(pure_interp *interp)
{
  assert(interp);
  interpreter::g_interp = (interpreter*)interp;
}

extern "C"
pure_interp *pure_current_interp()
{
  return (pure_interp*)interpreter::g_interp;
}

/* END OF PUBLIC API. *******************************************************/

extern "C"
pure_expr *pure_const(int32_t tag)
{
  // XXXFIXME: We should cache these on a per interpreter basis, so that only
  // a single expression node exists for each symbol.
  pure_expr *x = new_expr();
  x->tag = tag;
  x->data.clos = 0;
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_clos(bool local, bool thunked, int32_t tag, uint32_t n,
		     void *f, void *e, uint32_t m, /* m x pure_expr* */ ...)
{
  // Parameterless closures are always thunked, otherwise they would already
  // have been executed.
  if (n==0) thunked = true;
  pure_expr *x = new_expr();
  x->tag = tag;
  x->data.clos = new pure_closure;
  x->data.clos->local = local;
  x->data.clos->thunked = thunked;
  x->data.clos->n = n;
  x->data.clos->m = m;
  x->data.clos->fp = f;
  x->data.clos->ep = e;
  if (e) ((Env*)e)->refc++;
  if (m == 0)
    x->data.clos->env = 0;
  else {
    x->data.clos->env = new pure_expr*[m];
    va_list ap;
    va_start(ap, m);
    for (size_t i = 0; i < m; i++) {
      x->data.clos->env[i] = va_arg(ap, pure_expr*);
      assert(x->data.clos->env[i]->refc > 0);
    }
    va_end(ap);
  }
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_long(int64_t l)
{
  int sgn = (l>0)?1:(l<0)?-1:0;
  uint64_t v = (uint64_t)(l>=0?l:-l);
  if (sizeof(mp_limb_t) == 8) {
    // 8 byte limbs, value fits in a single limb.
    limb_t u[1] = { v };
    return pure_bigint(sgn, u);
  } else {
    // 4 byte limbs, put least significant word in the first limb.
    limb_t u[2] = { (uint32_t)v, (uint32_t)(v>>32) };
    return pure_bigint(sgn+sgn, u);
  }
}

static void make_bigint(mpz_t z, int32_t size, const limb_t *limbs)
{
  // FIXME: For efficiency, we poke directly into the mpz struct here, this
  // might need to be reviewed for future GMP revisions.
  int sz = size>=0?size:-size, sgn = size>0?1:size<0?-1:0, sz0 = 0;
  // normalize: the most significant limb should be nonzero
  for (int i = 0; i < sz; i++) if (limbs[i] != 0) sz0 = i+1;
  sz = sz0; size = sgn*sz;
  mpz_init(z);
  if (sz > 0) _mpz_realloc(z, sz);
  assert(sz == 0 || z->_mp_d);
  for (int i = 0; i < sz; i++) z->_mp_d[i] = limbs[i];
  z->_mp_size = size;
}

extern "C"
pure_expr *pure_bigint(int32_t size, const limb_t *limbs)
{
  pure_expr *x = new_expr();
  x->tag = EXPR::BIGINT;
  make_bigint(x->data.z, size, limbs);
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
int32_t pure_cmp_bigint(pure_expr *x, int32_t size, const limb_t *limbs)
{
  assert(x && x->tag == EXPR::BIGINT);
  mpz_t z;
  make_bigint(z, size, limbs);
  int res = mpz_cmp(x->data.z, z);
  mpz_clear(z);
  return res;
}

extern "C"
int32_t pure_cmp_string(pure_expr *x, const char *s)
{
  assert(x && x->tag == EXPR::STR);
  return strcmp(x->data.s, s);
}

list<char*> temps; // XXXFIXME: This should be TLD.

char *pure_get_cstring(pure_expr *x)
{
  assert(x && x->tag == EXPR::STR);
  char *s = fromutf8(x->data.s, 0);
  assert(s);
  temps.push_back(s);
  return s;
}

extern "C"
void pure_free_cstrings()
{
  for (list<char*>::iterator t = temps.begin(); t != temps.end(); t++)
    if (*t) free(*t);
  temps.clear();
}

extern "C"
int64_t pure_get_long(pure_expr *x)
{
  uint64_t v =
    (sizeof(mp_limb_t) == 8) ? (uint64_t)mpz_getlimbn(x->data.z, 0) :
    (mpz_getlimbn(x->data.z, 0) +
     (((uint64_t)mpz_getlimbn(x->data.z, 1))<<32));
  return (mpz_sgn(x->data.z) < 0) ? -(int64_t)v : (int64_t)v;
}

extern "C"
int32_t pure_get_int(pure_expr *x)
{
  uint32_t v =
    (sizeof(mp_limb_t) == 8) ? (uint32_t)(uint64_t)mpz_getlimbn(x->data.z, 0) :
    mpz_getlimbn(x->data.z, 0);
  return (mpz_sgn(x->data.z) < 0) ? -(int32_t)v : (int32_t)v;
}

extern "C"
void *pure_get_bigint(pure_expr *x)
{
  assert(x && x->tag == EXPR::BIGINT);
  return &x->data.z;
}

static inline pure_expr* bad_matrix_exception(pure_expr *x)
{
  if (!interpreter::g_interp) return 0;
  pure_expr *f = pure_const(interpreter::g_interp->symtab.bad_matrix_sym().f);
  if (x)
    return pure_apply2(f, x);
  else
    return f;
}

extern "C"
pure_expr *pure_matrix_rows(uint32_t n, ...)
{
#ifdef HAVE_GSL
  va_list ap;
  va_start(ap, n);
  pure_expr **xs = (pure_expr**)alloca(n*sizeof(pure_expr*));
  size_t m = 0; int k = -1;
  size_t nrows = 0, ncols = 0;
  int32_t target = EXPR::IMATRIX;
  for (size_t i = 0; i < n; i++) {
    pure_expr *x = va_arg(ap, pure_expr*);
    switch (x->tag) {
    case EXPR::DBL:
      if (target == EXPR::IMATRIX) target = EXPR::MATRIX;
    case EXPR::INT:
    case EXPR::BIGINT:
      if (k >= 0 && k != 1) pure_throw(bad_matrix_exception(x));
      xs[m++] = x; nrows++;
      k = 1;
      break;
    case EXPR::APP: {
      pure_expr *u = x->data.x[0], *v = x->data.x[1];
      if (u->tag == EXPR::APP) {
	interpreter& interp = *interpreter::g_interp;
	pure_expr *f = u->data.x[0];
	symbol *rect = interp.symtab.complex_rect_sym(),
	  *polar = interp.symtab.complex_polar_sym();
	if ((!rect || f->tag != rect->f) &&
	    (!polar || f->tag != polar->f) &&
	    f->tag != interp.symtab.pair_sym().f)
	  pure_throw(bad_matrix_exception(x));
	u = u->data.x[1];
	if (u->tag != EXPR::INT && u->tag != EXPR::BIGINT &&
	    u->tag != EXPR::DBL)
	  pure_throw(bad_matrix_exception(x));
	if (v->tag != EXPR::INT && v->tag != EXPR::BIGINT &&
	    v->tag != EXPR::DBL)
	  pure_throw(bad_matrix_exception(x));
      } else
	pure_throw(bad_matrix_exception(x));
      if (k >= 0 && k != 1) pure_throw(bad_matrix_exception(x));
      target = EXPR::CMATRIX;
      xs[m++] = x; nrows++;
      k = 1;
      break;
    }
    case EXPR::MATRIX: {
      gsl_matrix *mp = (gsl_matrix*)x->data.mat.p;
      if (mp) {
	if (k >= 0 && mp->size2 != (size_t)k)
	  pure_throw(bad_matrix_exception(x));
	if (mp->size1 > 0) xs[m++] = x;
	nrows += mp->size1;
	k = mp->size2;
      } else if (k>0)
	pure_throw(bad_matrix_exception(x));
      if (target == EXPR::IMATRIX) target = EXPR::MATRIX;
      break;
    }
    case EXPR::CMATRIX: {
      gsl_matrix_complex *mp = (gsl_matrix_complex*)x->data.mat.p;
      if (mp) {
	if (k >= 0 && mp->size2 != (size_t)k)
	  pure_throw(bad_matrix_exception(x));
	if (mp->size1 > 0) xs[m++] = x;
	nrows += mp->size1;
	k = mp->size2;
      } else if (k>0)
	pure_throw(bad_matrix_exception(x));
      target = EXPR::CMATRIX;
      break;
    }
    case EXPR::IMATRIX: {
      gsl_matrix_int *mp = (gsl_matrix_int*)x->data.mat.p;
      if (mp) {
	if (k >= 0 && mp->size2 != (size_t)k)
	  pure_throw(bad_matrix_exception(x));
	if (mp->size1 > 0) xs[m++] = x;
	nrows += mp->size1;
	k = mp->size2;
      } else if (k>0)
	pure_throw(bad_matrix_exception(x));
      break;
    }
    default:
      pure_throw(bad_matrix_exception(x));
      break;
    }
  }
  va_end(ap);
  if (k < 0) k = 0;
  ncols = k;
  if (nrows == 0 && ncols == 0) target = EXPR::MATRIX;
  pure_expr *ret = 0;
  switch (target) {
  case EXPR::MATRIX:
    ret = double_matrix_rows(nrows, ncols, m, xs);
    break;
#if 0 // XXXTODO
  case EXPR::CMATRIX:
    ret = complex_matrix_rows(nrows, ncols, m, xs);
    break;
#endif
  case EXPR::IMATRIX:
    ret = int_matrix_rows(nrows, ncols, m, xs);
    break;
  default:
    break;
  }
  if (!ret) pure_throw(not_implemented_exception());
  return ret;
#else
  pure_throw(not_implemented_exception());
  return 0;
#endif
}

extern "C"
pure_expr *pure_matrix_columns(uint32_t n, ...)
{
#ifdef HAVE_GSL
  va_list ap;
  va_start(ap, n);
  pure_expr **xs = (pure_expr**)alloca(n*sizeof(pure_expr*));
  size_t m = 0; int k = -1;
  size_t nrows = 0, ncols = 0;
  int32_t target = EXPR::IMATRIX;
  for (size_t i = 0; i < n; i++) {
    pure_expr *x = va_arg(ap, pure_expr*);
    switch (x->tag) {
    case EXPR::DBL:
      if (target == EXPR::IMATRIX) target = EXPR::MATRIX;
    case EXPR::INT:
    case EXPR::BIGINT:
      if (k >= 0 && k != 1) pure_throw(bad_matrix_exception(x));
      xs[m++] = x; ncols++;
      k = 1;
      break;
    case EXPR::APP: {
      pure_expr *u = x->data.x[0], *v = x->data.x[1];
      if (u->tag == EXPR::APP) {
	interpreter& interp = *interpreter::g_interp;
	pure_expr *f = u->data.x[0];
	symbol *rect = interp.symtab.complex_rect_sym(),
	  *polar = interp.symtab.complex_polar_sym();
	if ((!rect || f->tag != rect->f) &&
	    (!polar || f->tag != polar->f) &&
	    f->tag != interp.symtab.pair_sym().f)
	  pure_throw(bad_matrix_exception(x));
	u = u->data.x[1];
	if (u->tag != EXPR::INT && u->tag != EXPR::BIGINT &&
	    u->tag != EXPR::DBL)
	  pure_throw(bad_matrix_exception(x));
	if (v->tag != EXPR::INT && v->tag != EXPR::BIGINT &&
	    v->tag != EXPR::DBL)
	  pure_throw(bad_matrix_exception(x));
      } else
	pure_throw(bad_matrix_exception(x));
      if (k >= 0 && k != 1) pure_throw(bad_matrix_exception(x));
      target = EXPR::CMATRIX;
      xs[m++] = x; ncols++;
      k = 1;
      break;
    }
    case EXPR::MATRIX: {
      gsl_matrix *mp = (gsl_matrix*)x->data.mat.p;
      if (mp) {
	if (k >= 0 && mp->size1 != (size_t)k)
	  pure_throw(bad_matrix_exception(x));
	if (mp->size2 > 0) xs[m++] = x;
	ncols += mp->size2;
	k = mp->size1;
      } else if (k>0)
	pure_throw(bad_matrix_exception(x));
      if (target == EXPR::IMATRIX) target = EXPR::MATRIX;
      break;
    }
    case EXPR::CMATRIX: {
      gsl_matrix_complex *mp = (gsl_matrix_complex*)x->data.mat.p;
      if (mp) {
	if (k >= 0 && mp->size1 != (size_t)k)
	  pure_throw(bad_matrix_exception(x));
	if (mp->size2 > 0) xs[m++] = x;
	ncols += mp->size2;
	k = mp->size1;
      } else if (k>0)
	pure_throw(bad_matrix_exception(x));
      break;
    }
    case EXPR::IMATRIX: {
      gsl_matrix_int *mp = (gsl_matrix_int*)x->data.mat.p;
      if (mp) {
	if (k >= 0 && mp->size2 != (size_t)k)
	  pure_throw(bad_matrix_exception(x));
	if (mp->size1 > 0) xs[m++] = x;
	ncols += mp->size2;
	k = mp->size2;
      } else if (k>0)
	pure_throw(bad_matrix_exception(x));
      target = EXPR::CMATRIX;
      break;
    }
    default:
      pure_throw(bad_matrix_exception(x));
      break;
    }
  }
  va_end(ap);
  if (k < 0) k = 0;
  nrows = k;
  if (nrows == 0 && ncols == 0) target = EXPR::MATRIX;
  pure_expr *ret = 0;
  switch (target) {
  case EXPR::MATRIX:
    ret = double_matrix_columns(nrows, ncols, m, xs);
    break;
#if 0 // XXXTODO
  case EXPR::CMATRIX:
    ret = complex_matrix_columns(nrows, ncols, m, xs);
    break;
#endif
  case EXPR::IMATRIX:
    ret = int_matrix_columns(nrows, ncols, m, xs);
    break;
  default:
    break;
  }
  if (!ret) pure_throw(not_implemented_exception());
  return ret;
#else
  pure_throw(not_implemented_exception());
  return 0;
#endif
}

extern "C"
pure_expr *pure_call(pure_expr *x)
{
  char test;
  assert(x);
  if (x->tag > 0 && x->data.clos && x->data.clos->n == 0) {
    void *fp = x->data.clos->fp;
#if DEBUG>1
    cerr << "pure_call: calling " << x << " -> " << fp << endl;
#endif
    assert(x->refc > 0 && !x->data.clos->local);
    // parameterless call
    checkall(test);
    return ((pure_expr*(*)())fp)();
  } else {
#if DEBUG>2
    if (x->tag >= 0 && x->data.clos)
      cerr << "pure_call: returning " << x << " -> " << x->data.clos->fp
	   << " (" << x->data.clos->n << " args)" << endl;
    else
      cerr << "pure_call: returning " << x << endl;
#endif
    return x;
  }
}

static inline void resize_sstk(pure_expr**& sstk, size_t& cap,
			       size_t sz, size_t n)
{
  size_t newsz = sz+n;
  if (newsz > cap) {
    while (newsz > cap) {
      assert((cap << 1) > cap);
      cap = cap << 1;
    }
    sstk = (pure_expr**)realloc(sstk, cap*sizeof(pure_expr*));
    assert(sstk);
  }
}

#define is_thunk(x) ((x)->tag == 0 && (x)->data.clos && (x)->data.clos->n == 0)

extern "C"
pure_expr *pure_force(pure_expr *x)
{
  char test;
  assert(x);
  if (is_thunk(x)) {
    // parameterless anonymous closure (thunk)
    assert(x->data.clos->thunked);
    pure_expr *ret;
    interpreter& interp = *interpreter::g_interp;
    void *fp = x->data.clos->fp;
    size_t m = x->data.clos->m;
    uint32_t env = 0;
    assert(x->refc > 0);
    // construct a stack frame for the function call
    if (m>0) {
      size_t sz = interp.sstk_sz;
      resize_sstk(interp.sstk, interp.sstk_cap, sz, m+1);
      pure_expr **sstk = interp.sstk;
      env = sz+1;
      sstk[sz++] = 0;
      for (size_t j = 0; j < m; j++) {
	sstk[sz++] = x->data.clos->env[j];
	assert(x->data.clos->env[j]->refc > 0);
	x->data.clos->env[j]->refc++;
      }
#if SSTK_DEBUG
      cerr << "++ stack: (sz = " << sz << ")\n";
      for (size_t i = 0; i < sz; i++) {
	pure_expr *x = sstk[i];
	if (i == interp.sstk_sz) cerr << "** pushed:\n";
	if (x)
	  cerr << i << ": " << (void*)x << ": " << x << endl;
	else
	  cerr << i << ": " << "** frame **\n";
      }
#endif
      interp.sstk_sz = sz;
    }
#if DEBUG>1
    cerr << "pure_force: calling " << x << " -> " << fp << endl;
    for (size_t j = 0; j < m; j++)
      cerr << "env#" << j << " = " << x->data.clos->env[j] << " -> " << (void*)x->data.clos->env[j] << ", refc = " << x->data.clos->env[j]->refc << endl;
#endif
    // parameterless call
    checkall(test);
    if (m>0)
      ret = ((pure_expr*(*)(uint32_t))fp)(env);
    else
      ret = ((pure_expr*(*)())fp)();
#if DEBUG>1
    cerr << "pure_force: result " << x << " = " << ret << " -> " << (void*)ret << ", refc = " << ret->refc << endl;
#endif
    // check whether the result is again a thunk, then we have to evaluate
    // that recursively
    if (is_thunk(ret))
      pure_force(pure_new_internal(ret));
    pure_new_internal(ret);
    // memoize the result
    assert(x!=ret);
    pure_free_clos(x);
    x->tag = ret->tag;
    x->data = ret->data;
    switch (x->tag) {
    case EXPR::APP:
      pure_new_internal(x->data.x[0]);
      pure_new_internal(x->data.x[1]);
    case EXPR::PTR:
      if (x->data.x[2]) pure_new_internal(x->data.x[2]);
      break;
    case EXPR::STR:
      x->data.s = strdup(x->data.s);
      break;
    default:
      if (x->tag >= 0 && x->data.clos)
	x->data.clos = pure_copy_clos(x->data.clos);
      break;
    }
    pure_free_internal(ret);
    return x;
  } else {
#if DEBUG>2
    if (x->tag >= 0 && x->data.clos)
      cerr << "pure_force: returning " << x << " -> " << x->data.clos->fp
	   << " (" << x->data.clos->n << " args)" << endl;
    else
      cerr << "pure_force: returning " << x << endl;
#endif
    return x;
  }
}

extern "C"
pure_expr *pure_apply(pure_expr *x, pure_expr *y)
{
  char test;
  assert(x && y && x->refc > 0 && y->refc > 0);
  // if the function in this call is a thunk, evaluate it now
  if (is_thunk(x)) pure_force(x);
  // travel down the spine, count arguments
  pure_expr *f = x, *f0, *ret;
  uint32_t n = 1;
  while (f->tag == EXPR::APP) { f = f->data.x[0]; n++; }
  f0 = f;
  if (f->tag >= 0 && f->data.clos && !f->data.clos->thunked &&
      f->data.clos->n == n) {
    // saturated call; execute it now
    interpreter& interp = *interpreter::g_interp;
    void *fp = f->data.clos->fp;
    size_t m = f->data.clos->m;
    uint32_t env = 0;
    void **argv = (void**)alloca(n*sizeof(void*));
    assert(argv && "pure_apply: stack overflow");
    assert(n <= MAXARGS && "pure_apply: function call exceeds maximum #args");
    assert(f->data.clos->local || m == 0);
    // collect arguments
    f = x;
    for (size_t j = 1; f->tag == EXPR::APP; j++, f = f->data.x[0]) {
      assert(f->data.x[1]->refc > 0);
      argv[n-1-j] = f->data.x[1]; f->data.x[1]->refc++;
    }
    argv[n-1] = y;
    // make sure that we do not gc the function before calling it
    f0->refc++; pure_free_internal(x);
    // first push the function object on the shadow stack so that it's
    // garbage-collected in case of an exception
    resize_sstk(interp.sstk, interp.sstk_cap, interp.sstk_sz, n+m+2);
    interp.sstk[interp.sstk_sz++] = f0;
    // construct a stack frame for the function call
    {
      size_t sz = interp.sstk_sz;
      resize_sstk(interp.sstk, interp.sstk_cap, sz, n+m+1);
      pure_expr **sstk = interp.sstk;
      if (m>0) env = sz+n+1;
      sstk[sz++] = 0;
      for (size_t j = 0; j < n; j++)
	sstk[sz++] = (pure_expr*)argv[j];
      for (size_t j = 0; j < m; j++) {
	sstk[sz++] = f0->data.clos->env[j];
	assert(f0->data.clos->env[j]->refc > 0);
	f0->data.clos->env[j]->refc++;
      }
#if SSTK_DEBUG
      cerr << "++ stack: (sz = " << sz << ")\n";
      for (size_t i = 0; i < sz; i++) {
	pure_expr *x = sstk[i];
	if (i == interp.sstk_sz) cerr << "** pushed:\n";
	if (x)
	  cerr << i << ": " << (void*)x << ": " << x << endl;
	else
	  cerr << i << ": " << "** frame **\n";
      }
#endif
      interp.sstk_sz = sz;
    }
#if DEBUG>1
    cerr << "pure_apply: calling " << f0 << " -> " << fp << endl;
    for (size_t j = 0; j < n; j++)
      cerr << "arg#" << j << " = " << (pure_expr*)argv[j] << " -> " << argv[j] << ", refc = " << ((pure_expr*)argv[j])->refc << endl;
    for (size_t j = 0; j < m; j++)
      cerr << "env#" << j << " = " << f0->data.clos->env[j] << " -> " << (void*)f0->data.clos->env[j] << ", refc = " << f0->data.clos->env[j]->refc << endl;
#endif
    checkall(test);
    if (m>0)
      xfuncall(ret, fp, n, env, argv)
    else
      funcall(ret, fp, n, argv)
#if DEBUG>1
	cerr << "pure_apply: result " << f0 << " = " << ret << " -> " << (void*)ret << ", refc = " << ret->refc << endl;
#endif
    // pop the function object from the shadow stack
    pure_free_internal(interp.sstk[--interp.sstk_sz]);
    return ret;
  } else {
    // construct a literal application node
    f = new_expr();
    f->tag = EXPR::APP;
    f->data.x[0] = x;
    f->data.x[1] = y;
    MEMDEBUG_NEW(f)
    return f;
  }
}

extern "C"
void pure_throw(pure_expr* e)
{
  interpreter::brkflag = 0;
  interpreter& interp = *interpreter::g_interp;
  if (interp.estk.empty())
    abort(); // no exception handler, bail out
  else {
    interp.estk.front().e = e;
    longjmp(interp.estk.front().jmp, 1);
  }
}

#include <signal.h>

extern "C"
void pure_sigfpe(void)
{
  pure_throw(signal_exception(SIGFPE));
}

static void sig_handler(int sig)
{
  interpreter::brkflag = sig;
}

extern "C"
void pure_trap(int32_t action, int32_t sig)
{
  if (action > 0)
    signal(sig, sig_handler);
  else if (action < 0)
    signal(sig, SIG_IGN);
  else
    signal(sig, SIG_DFL);
}

extern "C"
pure_expr *pure_catch(pure_expr *h, pure_expr *x)
{
  char test;
  assert(h && x);
  if (x->tag >= 0 && x->data.clos && x->data.clos->n == 0) {
    interpreter& interp = *interpreter::g_interp;
    void *fp = x->data.clos->fp;
#if DEBUG>1
    cerr << "pure_catch: calling " << x << " -> " << fp << endl;
#endif
    assert(h->refc > 0 && x->refc > 0);
    size_t m = x->data.clos->m;
    assert(x->data.clos->local || m == 0);
    pure_expr **env = 0;
    size_t oldsz = interp.sstk_sz;;
    if (m>0) {
      // construct a stack frame
      size_t sz = oldsz;
      resize_sstk(interp.sstk, interp.sstk_cap, sz, m+1);
      pure_expr **sstk = interp.sstk; env = sstk+sz+1;
      sstk[sz++] = 0;
      for (size_t j = 0; j < m; j++) {
	sstk[sz++] = x->data.clos->env[j];
	assert(env[j]->refc > 0); env[j]->refc++;
      }
#if SSTK_DEBUG
      cerr << "++ stack: (sz = " << sz << ")\n";
      for (size_t i = 0; i < sz; i++) {
	pure_expr *x = sstk[i];
	if (i == interp.sstk_sz) cerr << "** pushed:\n";
	if (x)
	  cerr << i << ": " << (void*)x << ": " << x << endl;
	else
	  cerr << i << ": " << "** frame **\n";
      }
#endif
      interp.sstk_sz = sz;
    }
    checkstk(test);
    // Push an exception.
    pure_exception ex; ex.e = 0; ex.sz = oldsz;
    interp.estk.push_front(ex);
    // Call the function now. Catch exceptions generated by the runtime.
    if (setjmp(interp.estk.front().jmp)) {
      // caught an exception
      size_t sz = interp.estk.front().sz;
      pure_expr *e = interp.estk.front().e;
      interp.estk.pop_front();
      if (e) pure_new_internal(e);
#if 0
      /* This doesn't seem to be safe here. Defer until later. */
      // collect garbage
      pure_expr *tmps = interp.tmps;
      while (tmps) {
	pure_expr *next = tmps->xp;
	pure_freenew(tmps);
	tmps = next;
      }
#endif
      for (size_t i = interp.sstk_sz; i-- > sz; )
	if (interp.sstk[i] && interp.sstk[i]->refc > 0)
	  pure_free_internal(interp.sstk[i]);
      interp.sstk_sz = sz;
      if (!e)
	e = pure_new_internal(pure_const(interp.symtab.void_sym().f));
      assert(e && e->refc > 0);
#if DEBUG>1
      cerr << "pure_catch: exception " << (void*)e << " (refc = " << e->refc
	   << "): " << e << endl;
#endif
      pure_free_internal(x);
      // mask further breaks until the handler starts executing
      interp.brkmask = 1;
      pure_expr *res = pure_apply(h, e);
      return res;
    } else {
      pure_expr *res;
      if (env)
	// pass environment
	res = ((pure_expr*(*)(uint32_t))fp)(env-interp.sstk);
      else
	// parameterless call
	res = ((pure_expr*(*)())fp)();
      // normal return
      interp.estk.pop_front();
#if DEBUG>2
      pure_expr *tmps = interp.tmps;
      while (tmps) {
	if (tmps != res) cerr << "uncollected temporary: " << tmps << endl;
	tmps = tmps->xp;
      }
#endif
      assert(res);
      res->refc++;
      pure_free_internal(h); pure_free_internal(x);
      pure_unref_internal(res);
      return res;
    }
  } else {
    pure_free_internal(h);
    pure_unref_internal(x);
    return x;
  }
}

extern "C"
pure_expr *pure_invoke(void *f, pure_expr** _e)
{
  assert(_e);
  pure_expr*& e = *_e;
  interpreter& interp = *interpreter::g_interp;
  // Cast the function pointer to the right type (takes no arguments, returns
  // a pure_expr*), so we can call it as a native function.
  pure_expr *(*fp)() = (pure_expr*(*)())f;
#if DEBUG>1
  cerr << "pure_invoke: calling " << f << endl;
#endif
  MEMDEBUG_INIT
  // Push an exception.
  pure_exception ex; ex.e = 0; ex.sz = interp.sstk_sz;
  interp.estk.push_front(ex);
  // Call the function now. Catch exceptions generated by the runtime.
  if (setjmp(interp.estk.front().jmp)) {
    // caught an exception
    size_t sz = interp.estk.front().sz;
    e = interp.estk.front().e;
    interp.estk.pop_front();
    if (e) pure_new_internal(e);
#if 0
    /* This doesn't seem to be safe here. Defer until later. */
    // collect garbage
    pure_expr *tmps = interp.tmps;
    while (tmps) {
      pure_expr *next = tmps->xp;
      pure_freenew(tmps);
      tmps = next;
    }
#endif
    for (size_t i = interp.sstk_sz; i-- > sz; )
      if (interp.sstk[i] && interp.sstk[i]->refc > 0)
	pure_free_internal(interp.sstk[i]);
    interp.sstk_sz = sz;
#if DEBUG>1
    if (e)
      cerr << "pure_invoke: exception " << (void*)e << " (refc = " << e->refc
	   << "): " << e << endl;
#endif
    MEMDEBUG_SUMMARY(e)
    return 0;
  } else {
    pure_expr *res = fp();
    // normal return
    interp.estk.pop_front();
    MEMDEBUG_SUMMARY(res)
#if DEBUG>2
    pure_expr *tmps = interp.tmps;
    while (tmps) {
      if (tmps != res) cerr << "uncollected temporary: " << tmps << endl;
      tmps = tmps->xp;
    }
#endif
    return res;
  }
}

extern "C"
void pure_new_args(uint32_t n, ...)
{
  va_list ap;
  va_start(ap, n);
  while (n-- > 0) {
    pure_expr *x = va_arg(ap, pure_expr*);
    if (x->refc > 0)
      x->refc++;
    else
      pure_new_internal(x);
  };
  va_end(ap);
}

extern "C"
void pure_free_args(pure_expr *x, uint32_t n, ...)
{
  va_list ap;
  if (x) x->refc++;
  va_start(ap, n);
  while (n-- > 0) {
    pure_expr *x = va_arg(ap, pure_expr*);
    if (x->refc > 1)
      x->refc--;
    else
      pure_free_internal(x);
  };
  va_end(ap);
}

extern "C"
uint32_t pure_push_args(uint32_t n, uint32_t m, ...)
{
  va_list ap;
  interpreter& interp = *interpreter::g_interp;
  size_t sz = interp.sstk_sz;
  resize_sstk(interp.sstk, interp.sstk_cap, sz, n+m+1);
  pure_expr **sstk = interp.sstk; uint32_t env = (m>0)?sz+n+1:0;
  // mark the beginning of this frame
  sstk[sz++] = 0;
  va_start(ap, m);
  for (size_t i = 0; i < n+m; i++) {
    pure_expr *x = va_arg(ap, pure_expr*);
    sstk[sz++] = x;
    if (x->refc > 0)
      x->refc++;
    else
      pure_new_internal(x);
  };
  va_end(ap);
#if SSTK_DEBUG
  cerr << "++ stack: (sz = " << sz << ")\n";
  for (size_t i = 0; i < sz; i++) {
    pure_expr *x = sstk[i];
    if (i == interp.sstk_sz) cerr << "** pushed:\n";
    if (x)
      cerr << i << ": " << (void*)x << ": " << x << endl;
    else
      cerr << i << ": " << "** frame **\n";
  }
#endif
  interp.sstk_sz = sz;
  // return a pointer to the environment:
  return env;
}

extern "C"
void pure_pop_args(pure_expr *x, uint32_t n, uint32_t m)
{
  interpreter& interp = *interpreter::g_interp;
  pure_expr **sstk = interp.sstk;
  size_t sz = interp.sstk_sz;
#if !defined(NDEBUG) || SSTK_DEBUG
  size_t oldsz = sz;
#endif
  sz -= n+m+1;
  assert(sz < oldsz && !sstk[sz]);
#if SSTK_DEBUG
  cerr << "++ stack: (oldsz = " << oldsz << ")\n";
  for (size_t i = 0; i < oldsz; i++) {
    pure_expr *x = sstk[i];
    if (i == sz) cerr << "** popped:\n";
    if (x)
      cerr << i << ": " << (void*)x << ": " << x << endl;
    else
      cerr << i << ": " << "** frame **\n";
  }
#endif
  if (x) x->refc++;
  for (size_t i = 0; i < n+m; i++) {
    pure_expr *x = sstk[sz+1+i];
    assert(x);
    if (x->refc > 1)
      x->refc--;
    else
      pure_free_internal(x);
  };
  interp.sstk_sz = sz;
}

extern "C"
void pure_pop_tail_args(pure_expr *x, uint32_t n, uint32_t m)
{
  interpreter& interp = *interpreter::g_interp;
  pure_expr **sstk = interp.sstk;
  size_t sz, lastsz = interp.sstk_sz, oldsz = lastsz;
  while (lastsz > 0 && sstk[--lastsz]) ;
  assert(lastsz < oldsz && !sstk[lastsz]);
  sz = lastsz-(n+m+1);
  assert(sz < lastsz && !sstk[sz]);
#if SSTK_DEBUG
  cerr << "++ stack: (oldsz = " << oldsz << ", lastsz = " << lastsz << ")\n";
  for (size_t i = 0; i < oldsz; i++) {
    pure_expr *x = sstk[i];
    if (i == sz) cerr << "** popped:\n";
    if (i == lastsz) cerr << "** moved:\n";
    if (x)
      cerr << i << ": " << (void*)x << ": " << x << endl;
    else
      cerr << i << ": " << "** frame **\n";
  }
#endif
  if (x) x->refc++;
  for (size_t i = 0; i < n+m; i++) {
    pure_expr *x = sstk[sz+1+i];
    assert(x);
    if (x->refc > 1)
      x->refc--;
    else
      pure_free_internal(x);
  };
  memmove(sstk+sz, sstk+lastsz, (oldsz-lastsz)*sizeof(pure_expr*));
  interp.sstk_sz -= n+m+1;
}

extern "C"
void pure_push_arg(pure_expr *x)
{
  interpreter& interp = *interpreter::g_interp;
  size_t sz = interp.sstk_sz;
  resize_sstk(interp.sstk, interp.sstk_cap, sz, 2);
  pure_expr** sstk = interp.sstk;
  sstk[sz++] = 0; sstk[sz++] = x;
  if (x->refc > 0)
    x->refc++;
  else
    pure_new_internal(x);
#if SSTK_DEBUG
  cerr << "++ stack: (sz = " << sz << ")\n";
  for (size_t i = 0; i < sz; i++) {
    pure_expr *x = sstk[i];
    if (i == interp.sstk_sz) cerr << "** pushed:\n";
    if (x)
      cerr << i << ": " << (void*)x << ": " << x << endl;
    else
      cerr << i << ": " << "** frame **\n";
  }
#endif
  interp.sstk_sz = sz;
}

extern "C"
void pure_pop_arg()
{
#if SSTK_DEBUG
  pure_pop_args(0, 1, 0);
#else
  interpreter& interp = *interpreter::g_interp;
  pure_expr *x = interp.sstk[interp.sstk_sz-1];
  if (x->refc > 1)
    x->refc--;
  else
    pure_free_internal(x);
  interp.sstk_sz -= 2;
#endif
}

extern "C"
void pure_pop_tail_arg()
{
#if SSTK_DEBUG
  pure_pop_tail_args(0, 1, 0);
#else
  interpreter& interp = *interpreter::g_interp;
  pure_expr **sstk = interp.sstk;
  size_t lastsz = interp.sstk_sz, oldsz = lastsz;
  while (lastsz > 0 && sstk[--lastsz]) ;
  pure_expr *x = interp.sstk[lastsz-1];
  if (x->refc > 1)
    x->refc--;
  else
    pure_free_internal(x);
  memmove(sstk+lastsz-2, sstk+lastsz, (oldsz-lastsz)*sizeof(pure_expr*));
  interp.sstk_sz -= 2;
#endif
}

extern "C"
void pure_debug(int32_t tag, const char *format, ...)
{
  cout << "break at ";
  if (tag > 0)
    cout << interpreter::g_interp->symtab.sym(tag).s;
  else
    cout << "<<anonymous closure>>";
  cout << ": ";
  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  va_end(ap);
  static bool init = false;
  if (!init) {
    cout << "\n(Press 'x' to exit the interpreter, <cr> to continue.)";
    init = true;
  }
  cout << "\n: ";
  char ans;
  cin >> noskipws >> ans;
  bool bail_out = !cin.good() || ans=='x';
  while (cin.good() && ans != '\n') cin >> noskipws >> ans;
  if (!bail_out && !cin.good()) bail_out = true;
  if (bail_out) exit(0);
}

/* LIBRARY API. *************************************************************/

extern "C"
pure_expr *pure_byte_string(const char *s)
{
  if (!s) return pure_pointer(0);
  return pure_pointer(strdup(s));
}

extern "C"
pure_expr *pure_byte_cstring(const char *s)
{
  if (!s) return pure_pointer(0);
  return pure_pointer(fromutf8(s));
}

extern "C"
pure_expr *pure_intval(pure_expr *x)
{
  assert(x);
  if (is_thunk(x)) pure_force(x);
  switch (x->tag) {
  case EXPR::INT:	return x;
  case EXPR::BIGINT:	return pure_int(pure_get_int(x));
  case EXPR::DBL:	return pure_int((int32_t)x->data.d);
#if SIZEOF_VOID_P==8
    // Must cast to 64 bit here first, since on 64 bit systems g++ gives an
    // error when directly casting a 64 bit pointer to a 32 bit integer.
  case EXPR::PTR:	return pure_int((uint32_t)(uint64_t)x->data.p);
#else
  case EXPR::PTR:	return pure_int((uint32_t)x->data.p);
#endif
  default:		return 0;
  }
}

extern "C"
pure_expr *pure_dblval(pure_expr *x)
{
  assert(x);
  if (is_thunk(x)) pure_force(x);
  switch (x->tag) {
  case EXPR::INT:	return pure_double((double)x->data.i);
  case EXPR::BIGINT:	return pure_double(mpz_get_d(x->data.z));
  case EXPR::DBL:	return x;
  default:		return 0;
  }
}

extern "C"
pure_expr *pure_pointerval(pure_expr *x)
{
  assert(x);
  if (is_thunk(x)) pure_force(x);
  switch (x->tag) {
  case EXPR::PTR:	return x;
  case EXPR::STR:	return pure_pointer(x->data.s);
  case EXPR::INT:	return pure_pointer((void*)x->data.i);
  case EXPR::BIGINT:
    if (sizeof(mp_limb_t) == 8)
#if SIZEOF_VOID_P==8
      return pure_pointer((void*)mpz_getlimbn(x->data.z, 0));
#else
      return pure_pointer((void*)(uint32_t)mpz_getlimbn(x->data.z, 0));
#endif
    else
#if SIZEOF_VOID_P==8
      return pure_pointer((void*)(uint64_t)pure_get_long(x));
#else
      return pure_pointer((void*)(uint32_t)pure_get_int(x));
#endif
  default:		return 0;
  }
}

static pure_expr *pointer_to_bigint(void *p)
{
  if (sizeof(mp_limb_t) == 8) {
    // In this case the pointer value ought to fit into a single limb.
#if SIZEOF_VOID_P==8
    limb_t u[1] = { (uint64_t)p };
#else
    limb_t u[1] = { (uint64_t)(uint32_t)p };
#endif
    return pure_bigint(1, u);
  }
  // 4 byte limbs.
#if SIZEOF_VOID_P==8
  // 8 byte pointers, put least significant word in the first limb.
  assert(sizeof(void*) == 8);
  limb_t u[2] = { (uint32_t)(uint64_t)p, (uint32_t)(((uint64_t)p)>>32) };
  return pure_bigint(2, u);
#else
  // 4 byte pointers.
  limb_t u[1] = { (uint32_t)p };
  return pure_bigint(1, u);
#endif
}

extern "C"
pure_expr *pure_bigintval(pure_expr *x)
{
  assert(x);
  if (is_thunk(x)) pure_force(x);
  if (x->tag == EXPR::BIGINT)
    return x;
  else if (x->tag == EXPR::PTR)
    return pointer_to_bigint(x->data.p);
  else if (x->tag != EXPR::INT && x->tag != EXPR::DBL)
    return 0;
  else if (x->tag == EXPR::DBL &&
	   (is_nan(x->data.d) || is_nan(x->data.d-x->data.d)))
    pure_sigfpe();
  pure_expr *y = pure_bigint(0, 0);
  mpz_t& z = y->data.z;
  if (x->tag == EXPR::INT)
    mpz_set_si(z, x->data.i);
  else if (x->tag == EXPR::DBL)
    mpz_set_d(z, x->data.d);
  return y;
}

extern "C"
pure_expr *pure_rational(double d)
{
  pure_expr *u = pure_bigint(0, 0);
  pure_expr *v = pure_bigint(0, 0);
  mpz_t& x = u->data.z;
  mpz_t& y = v->data.z;
  mpq_t q;
  mpq_init(q);
  mpq_set_d(q, d);
  mpq_get_num(x, q);
  mpq_get_den(y, q);
  mpq_clear(q);
  return pure_tuplel(2, u, v);
}

// This is the ``Mersenne Twister'' random number generator MT19937, which
// generates pseudorandom integers uniformly distributed in 0..(2^32 - 1)
// starting from any odd seed in 0..(2^32 - 1).  This version is a recode
// by Shawn Cokus (Cokus@math.washington.edu) on March 8, 1998 of a version by
// Takuji Nishimura (who had suggestions from Topher Cooper and Marc Rieffel in
// July-August 1997).
//
// Effectiveness of the recoding (on Goedel2.math.washington.edu, a DEC Alpha
// running OSF/1) using GCC -O3 as a compiler: before recoding: 51.6 sec. to
// generate 300 million random numbers; after recoding: 24.0 sec. for the same
// (i.e., 46.5% of original time), so speed is now about 12.5 million random
// number generations per second on this machine.
//
// According to the URL <http://www.math.keio.ac.jp/~matumoto/emt.html>
// (and paraphrasing a bit in places), the Mersenne Twister is ``designed
// with consideration of the flaws of various existing generators,'' has
// a period of 2^19937 - 1, gives a sequence that is 623-dimensionally
// equidistributed, and ``has passed many stringent tests, including the
// die-hard test of G. Marsaglia and the load test of P. Hellekalek and
// S. Wegenkittl.''  It is efficient in memory usage (typically using 2506
// to 5012 bytes of static data, depending on data type sizes, and the code
// is quite short as well).  It generates random numbers in batches of 624
// at a time, so the caching and pipelining of modern systems is exploited.
// It is also divide- and mod-free.
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the GNU Library General Public License as published by
// the Free Software Foundation (either version 2 of the License or, at your
// option, any later version).  This library is distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY, without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
// the GNU Library General Public License for more details.  You should have
// received a copy of the GNU Library General Public License along with this
// library; if not, write to the Free Software Foundation, Inc., 59 Temple
// Place, Suite 330, Boston, MA 02111-1307, USA.
//
// The code as Shawn received it included the following notice:
//
//   Copyright (C) 1997 Makoto Matsumoto and Takuji Nishimura.  When
//   you use this, send an e-mail to <matumoto@math.keio.ac.jp> with
//   an appropriate reference to your work.
//
// It would be nice to CC: <Cokus@math.washington.edu> when you write.
//

// See http://www.math.keio.ac.jp/~matumoto/emt.html for the original sources.

#define N              (624)
#define M              (397)
#define K              (0x9908B0DFU)
#define hiBit(u)       ((u) & 0x80000000U)
#define loBit(u)       ((u) & 0x00000001U)
#define loBits(u)      ((u) & 0x7FFFFFFFU)
#define mixBits(u, v)  (hiBit(u)|loBits(v))

// TLD?
static uint32_t stateMT[N+1];
static uint32_t *nextMT;
static int leftMT = -1;

void pure_srandom(uint32_t seed)
{
  // MT works best with odd seeds, so we enforce that here.
  register uint32_t x = (seed | 1U) & 0xFFFFFFFFU, *s = stateMT;
  register int j;

  for (leftMT=0, *s++=x, j=N; --j; *s++ = (x*=69069U) & 0xFFFFFFFFU);
}

static uint32_t reloadMT(void)
{
  register uint32_t *p0=stateMT, *p2=stateMT+2, *pM=stateMT+M, s0, s1;
  register int j;

  if (leftMT < -1)
    pure_srandom(4357U);

  leftMT=N-1, nextMT=stateMT+1;

  for (s0=stateMT[0], s1=stateMT[1], j=N-M+1; --j; s0=s1, s1=*p2++)
    *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);

  for (pM=stateMT, j=M; --j; s0=s1, s1=*p2++)
    *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);

  s1=stateMT[0], *p0 = *pM ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
  s1 ^= (s1 >> 11);
  s1 ^= (s1 <<  7) & 0x9D2C5680U;
  s1 ^= (s1 << 15) & 0xEFC60000U;
  return(s1 ^ (s1 >> 18));
}

uint32_t pure_random(void)
{
  uint32_t y;
  
  if(--leftMT < 0)
    return reloadMT();

  y  = *nextMT++;
  y ^= (y >> 11);
  y ^= (y <<  7) & 0x9D2C5680U;
  y ^= (y << 15) & 0xEFC60000U;
  return (y ^ (y >> 18));
}

#undef N
#undef M
#undef K

extern "C"
pure_expr *bigint_neg(mpz_t x)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_neg(z, x);
  return u;
}

extern "C"
pure_expr *bigint_add(mpz_t x, mpz_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_add(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_sub(mpz_t x, mpz_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_sub(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_mul(mpz_t x, mpz_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_mul(z, x, y);
  return u;
}

// These raise a SIGFPE signal exception for division by zero.

extern "C"
pure_expr *bigint_div(mpz_t x, mpz_t y)
{
  if (mpz_sgn(y) == 0) pure_sigfpe();
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_tdiv_q(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_mod(mpz_t x, mpz_t y)
{
  if (mpz_sgn(y) == 0) pure_sigfpe();
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_tdiv_r(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_pow(mpz_t x, uint32_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_pow_ui(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_shl(mpz_t x, int32_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_mul_2exp(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_shr(mpz_t x, int32_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_fdiv_q_2exp(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_not(mpz_t x)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_com(z, x);
  return u;
}

extern "C"
pure_expr *bigint_and(mpz_t x, mpz_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_and(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_or(mpz_t x, mpz_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_ior(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_gcd(mpz_t x, mpz_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_gcd(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_lcm(mpz_t x, mpz_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_lcm(z, x, y);
  return u;
}

extern "C"
int32_t bigint_cmp(mpz_t x, mpz_t y)
{
  return mpz_cmp(x, y);
}

extern "C"
bool string_null(const char *s)
{
  assert(s);
  return *s==0;
}

extern "C"
uint32_t string_size(const char *s)
{
  assert(s);
  return u8strlen(s);
}

extern "C"
pure_expr *string_char_at(const char *s, uint32_t n)
{
  assert(s);
  unsigned long c = u8strchar(s, n);
  if (c == 0) return 0;
  char buf[5];
  return pure_string_dup(u8char(buf, c));
}

static void add_char(pure_expr*** x, unsigned long c)
{
  interpreter& interp = *interpreter::g_interp;
  char buf[5];
  pure_expr *y = new_expr(), *z = pure_string_dup(u8char(buf, c));
  y->tag = EXPR::APP;
  y->data.x[0] = pure_new_internal(pure_const(interp.symtab.cons_sym().f));
  y->data.x[1] = pure_new_internal(z);
  **x = pure_new_internal(new_expr());
  (**x)->tag = EXPR::APP;
  (**x)->data.x[0] = pure_new_internal(y);
  *x = (**x)->data.x+1;
}

extern "C"
pure_expr *string_chars(const char *s)
{
  assert(s);
  interpreter& interp = *interpreter::g_interp;
  pure_expr *x, **y = &x, ***z = &y;
  u8dostr(s, (void(*)(void*,unsigned long))add_char, z);
  **z = pure_new_internal(pure_const(interp.symtab.nil_sym().f));
  pure_unref_internal(x);
  return x;
}

extern "C"
pure_expr *string_chr(uint32_t n)
{
  char buf[5];
  return pure_string_dup(u8char(buf, n));
}

extern "C"
pure_expr *string_ord(const char *c)
{
  assert(c);
  long n = u8charcode(c);
  if (n > 0)
    return pure_int(n);
  else
    return 0;
}

extern "C"
pure_expr *string_concat(const char* s, const char *t)
{
  assert(s && t);
  size_t p = strlen(s), q = strlen(t);
  char *buf = new char[p+q+1];
  strcpy(buf, s); strcpy(buf+p, t);
  pure_expr *x = new_expr();
  x->tag = EXPR::STR;
  x->data.s = buf;
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *string_concat_list(pure_expr *xs)
{
  // linear-time concatenation of a list of strings
  assert(xs);
  if (is_thunk(xs)) pure_force(xs);
  // calculate the size of the result string
  pure_expr *ys = xs, *z, *zs;
  size_t n = 0;
  while (is_cons(ys, z, zs)) {
    if (is_thunk(z)) pure_force(z);
    if (z->tag != EXPR::STR) break;
    n += strlen(z->data.s);
    ys = zs;
    if (is_thunk(ys)) pure_force(ys);
  }
  if (!is_nil(ys)) return 0;
  // allocate the result string
  char *buf = new char[n+1]; buf[0] = 0;
  // concatenate
  ys = xs; n = 0;
  while (is_cons(ys, z, zs) && z->tag == EXPR::STR) {
    strcpy(buf+n, z->data.s);
    n += strlen(z->data.s);
    ys = zs;
  }
  // return the result
  pure_expr *x = new_expr();
  x->tag = EXPR::STR;
  x->data.s = buf;
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *string_substr(const char* s, uint32_t pos, uint32_t size)
{
  assert(s);
  const char *p = u8strcharpos(s, pos), *q = u8strcharpos(p, size);
  size_t n = q-p;
  char *buf = new char[n+1];
  strncpy(buf, p, n); buf[n] = 0;
  pure_expr *x = new_expr();
  x->tag = EXPR::STR;
  x->data.s = buf;
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
int32_t string_index(const char* s, const char *t)
{
  assert(s && t);
  const char *p = strstr(s, t);
  if (p)
    return u8strpos(s, p);
  else
    return -1;
}

extern "C"
char *str(const pure_expr *x)
{
  assert(x);
  ostringstream os;
  try {
    os << x;
    return strdup(os.str().c_str());
  } catch (err &e) {
    return 0;
  }
}

extern "C"
pure_expr *eval(const char *s)
{
  assert(s);
  interpreter& interp = *interpreter::g_interp;
  interp.errmsg.clear();
  pure_expr *res = interp.runstr(string(s)+";");
  interp.result = 0;
  if (res) pure_unref_internal(res);
  return res;
}

extern "C"
const char *lasterr()
{
  interpreter& interp = *interpreter::g_interp;
  return interp.errmsg.c_str();
}

static uint32_t mpz_hash(const mpz_t z)
{
  uint32_t h = 0;
  int i, len = z->_mp_size;
  if (len < 0) len = -len;
  if (sizeof(mp_limb_t) == 8) {
    for (i=0; i<len; i++) {
      h ^= (uint32_t)(uint64_t)z->_mp_d[i];
      h ^= (uint32_t)(((uint64_t)z->_mp_d[i])>>32);
    }
  } else {
    for (i=0; i<len; i++)
      h ^= z->_mp_d[i];
  }
  if (z->_mp_size < 0)
    h = -h;
  return h;
}

static uint32_t double_hash(double d)
{
  uint32_t h;
  char *c;
  size_t i;
  c = (char*)&d;
  for (h=0, i=0; i<sizeof(double); i++) {
    h += c[i] * 971;
  }
  return h;
}

static uint32_t string_hash(char *s)
{
  uint32_t h = 0, g;
  while (*s) {
    h = (h<<4)+*(s++);
    if ((g = (h & 0xf0000000)))	{
      h = h^(g>>24);
      h = h^g;
    }
  }
  return h;
}

extern "C"
uint32_t hash(pure_expr *x)
{
  char test;
  if (is_thunk(x)) pure_force(x);
  switch (x->tag) {
  case EXPR::INT:
    return (uint32_t)x->data.i;
  case EXPR::BIGINT:
    return mpz_hash(x->data.z);
  case EXPR::DBL:
    return double_hash(x->data.d);
  case EXPR::STR:
    return string_hash(x->data.s);
  case EXPR::PTR:
#if SIZEOF_VOID_P==8
    return ((uint32_t)(uint64_t)x->data.p) ^ ((uint32_t)(((uint64_t)x->data.p)>>32));
#else
    return (uint32_t)x->data.p;
#endif
  case EXPR::APP: {
    checkstk(test);
    int h;
    h = hash(x->data.x[0]);
    h = (h<<1) | (h<0 ? 1 : 0);
    h ^= hash(x->data.x[1]);
    return (uint32_t)h;
  }
  default:
    return (uint32_t)x->tag;
  }
}

extern "C"
bool same(pure_expr *x, pure_expr *y)
{
  char test;
  if (x == y)
    return 1;
  if (is_thunk(x)) pure_force(x);
  if (is_thunk(y)) pure_force(y);
  if (x->tag != y->tag)
    return 0;
  else if (x->tag >= 0 && y->tag >= 0)
    if (x->data.clos && y->data.clos)
      /* Note that for global functions the function pointers may differ in
	 some cases (specifically in the case of an external which may chain
	 to a Pure definition of the same global). However, in that case we
	 may safely assume that the functions are the same anyway, since they
	 are referred to by the same global symbol. */
      return !x->data.clos->local && !y->data.clos->local ||
	x->data.clos->fp == y->data.clos->fp;
    else
      return x->data.clos == y->data.clos;
  else {
    switch (x->tag) {
    case EXPR::APP: {
      checkstk(test);
      return same(x->data.x[0], y->data.x[0]) &&
	same(x->data.x[1], y->data.x[1]);
    }
    case EXPR::INT:
      return x->data.i == y->data.i;
    case EXPR::BIGINT:
      return mpz_cmp(x->data.z, y->data.z) == 0;
    case EXPR::DBL:
      return x->data.d == y->data.d || is_nan(x->data.d) && is_nan(y->data.d);
    case EXPR::STR:
      return strcmp(x->data.s, y->data.s) == 0;
    case EXPR::PTR:
      return x->data.p == y->data.p;
    default:
      return 1;
    }
  }
}

extern "C"
bool funp(const pure_expr *x)
{
  return (x->tag > 0 && x->data.clos);
}

extern "C"
bool lambdap(const pure_expr *x)
{
  return (x->tag == 0 && x->data.clos && x->data.clos->n > 0);
}

extern "C"
bool thunkp(const pure_expr *x)
{
  return (x->tag == 0 && x->data.clos && x->data.clos->n == 0);
}

extern "C"
bool varp(const pure_expr *x)
{
  return (x->tag > 0 && !x->data.clos);
}

extern "C"
int32_t pointer_get_byte(void *ptr)
{
  uint8_t *p = (uint8_t*)ptr;
  return *p;
}

extern "C"
int32_t pointer_get_int(void *ptr)
{
  int32_t *p = (int32_t*)ptr;
  return *p;
}

extern "C"
double pointer_get_double(void *ptr)
{
  double *p = (double*)ptr;
  return *p;
}

extern "C"
char *pointer_get_string(void *ptr)
{
  char **p = (char**)ptr;
  return *p;
}

extern "C"
void *pointer_get_pointer(void *ptr)
{
  void **p = (void**)ptr;
  return *p;
}

extern "C"
pure_expr *pointer_get_expr(void *ptr)
{
  pure_expr **p = (pure_expr**)ptr;
  return *p;
}

extern "C"
void pointer_put_byte(void *ptr, int32_t x)
{
  uint8_t *p = (uint8_t*)ptr;
  *p = x;
}

extern "C"
void pointer_put_int(void *ptr, int32_t x)
{
  int32_t *p = (int32_t*)ptr;
  *p = x;
}

extern "C"
void pointer_put_double(void *ptr, double x)
{
  double *p = (double*)ptr;
  *p = x;
}

extern "C"
void pointer_put_string(void *ptr, const char *x)
{
  char **p = (char**)ptr;
  *p = strdup(x);
}

extern "C"
void pointer_put_pointer(void *ptr, void *x)
{
  void **p = (void**)ptr;
  *p = x;
}

extern "C"
void pointer_put_expr(void *ptr, pure_expr *x)
{
  pure_expr **p = (pure_expr**)ptr;
  *p = x;
}

#include <errno.h>

extern "C"
int pure_errno(void)
{
  return errno;
}

extern "C"
void pure_set_errno(int value)
{
  errno = value;
}

#include <time.h>

extern "C"
int64_t pure_time(void)
{
  return (int64_t)time(NULL);
}

/* Note that the following are not thread-safe as they use statically
   allocated buffers. */

extern "C"
char *pure_ctime(int64_t t)
{
  time_t time = (time_t)t;
  return ctime(&time);
}

extern "C"
char *pure_gmtime(int64_t t)
{
  time_t time = (time_t)t;
  return asctime(gmtime(&time));
}

extern "C"
char *pure_strftime(const char *format, int64_t t)
{
  time_t time = (time_t)t;
  static char buf[1024];
  if (!strftime(buf, 1024, format, localtime(&time)))
    /* The interface to strftime is rather brain-damaged since it returns zero
       both in case of a buffer overflow and when the resulting string is
       empty. We just pretend that there cannot be any errors and return an
       empty string in both cases. */
    buf[0] = 0;
  return buf;
}

#ifdef HAVE_GETTIMEOFDAY
#include <sys/time.h>
extern "C"
double pure_gettimeofday(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((double)tv.tv_sec)+((double)tv.tv_usec)*1e-6;
}
#else
#ifdef HAVE_FTIME
#include <sys/timeb.h>
extern "C"
double pure_gettimeofday(void)
{
  struct timeb tb;
  ftime(&tb);
  return ((double)tb.time)+((double)tb.millitm)*1e-3;
}
#else
extern "C"
double pure_gettimeofday(void)
{
  return (double)time(NULL);
}
#endif
#endif

#ifdef __MINGW32__
#include <windows.h>
double pure_nanosleep(double t)
{
  if (t > 0.0) {
    unsigned long secs;
    unsigned short msecs;
    double ip, fp;
    if (t > LONG_MAX) t = LONG_MAX;
    fp = modf(t, &ip);
    secs = (unsigned long)ip;
    msecs = (unsigned short)(fp*1e3);
    Sleep(secs*1000U+msecs);
  }
  return 0.0;
}
#else
double pure_nanosleep(double t)
{
  if (t > 0.0) {
    double ip, fp;
    unsigned long secs;
#ifdef HAVE_NANOSLEEP
    unsigned long nsecs;
    struct timespec req, rem;
    fp = modf(t, &ip);
    if (ip > LONG_MAX) { ip = (double)LONG_MAX; fp = 0.0; }
    secs = (unsigned long)ip;
    nsecs = (unsigned long)(fp*1e9);
    req.tv_sec = secs; req.tv_nsec = nsecs;
    if (nanosleep(&req, &rem))
      return ((double)rem.tv_sec)+((double)rem.tv_nsec)*1e-9;
    else
      return 0.0;
#else
#ifdef HAVE_USLEEP
    unsigned long usecs;
    if (t > LONG_MAX) t = LONG_MAX;
    fp = modf(t, &ip);
    secs = (unsigned long)ip;
    usecs = (unsigned long)(fp*1e6);
    usleep(secs*1000000U+usecs);
    return 0.0;
#else
    fp = modf(t, &ip);
    if (ip > LONG_MAX) ip = (double)LONG_MAX;
    secs = (unsigned long)ip;
    return (double)sleep(secs);
#endif
#endif
  } else
    return 0.0;
}
#endif

#ifdef __MINGW32__
extern "C"
FILE *popen(const char *command, const char *type)
{
  return _popen(command, type);
}

extern "C"
int pclose(FILE *stream)
{
  return _pclose(stream);
}

extern "C"
unsigned int sleep(unsigned int secs)
{
  Sleep(secs*1000);
  return 0;
}
#endif

/* Horrible kludge to get round, trunc and the inverse hyperbolic functions
   from libmingwex.a (these are in C99, but not in the Windows system
   libraries, and LLVM doesn't know how to get them either). */

extern "C"
double __round(double x)
{
  return round(x);
}

extern "C"
double __trunc(double x)
{
  return trunc(x);
}

extern "C"
double __asinh(double x)
{
  return asinh(x);
}

extern "C"
double __acosh(double x)
{
  return acosh(x);
}

extern "C"
double __atanh(double x)
{
  return atanh(x);
}

extern "C"
int pure_fprintf(FILE *fp, const char *format)
{
  return fprintf(fp, format);
}

extern "C"
int pure_fprintf_int(FILE *fp, const char *format, int32_t x)
{
  return fprintf(fp, format, x);
}

extern "C"
int pure_fprintf_double(FILE *fp, const char *format, double x)
{
  return fprintf(fp, format, x);
}

extern "C"
int pure_fprintf_string(FILE *fp, const char *format, const char *x)
{
  return fprintf(fp, format, x);
}

extern "C"
int pure_fprintf_pointer(FILE *fp, const char *format, const void *x)
{
  return fprintf(fp, format, x);
}

extern "C"
int pure_snprintf(char *buf, size_t size, const char *format)
{
  return snprintf(buf, size, format);
}

extern "C"
int pure_snprintf_int(char *buf, size_t size, const char *format, int32_t x)
{
  return snprintf(buf, size, format, x);
}

extern "C"
int pure_snprintf_double(char *buf, size_t size, const char *format, double x)
{
  return snprintf(buf, size, format, x);
}

extern "C"
int pure_snprintf_string(char *buf, size_t size, const char *format, const char *x)
{
  return snprintf(buf, size, format, x);
}

extern "C"
int pure_snprintf_pointer(char *buf, size_t size, const char *format, const void *x)
{
  return snprintf(buf, size, format, x);
}

#define myformat(format) scanf_format((char*)alloca(strlen(format)+3), format)

static inline char *scanf_format(char *buf, const char *format)
{
  strcpy(buf, format); strcat(buf, "%n");
  return buf;
}

extern "C"
int pure_fscanf(FILE *fp, const char *format)
{
  int count = -1;
  fscanf(fp, myformat(format), &count);
  return count;
}

extern "C"
int pure_fscanf_int(FILE *fp, const char *format, int32_t *x)
{
  // wrap this up in case int on the target platform is not 32 bit
  int count = -1, y;
  fscanf(fp, myformat(format), &y, &count);
  if (count >= 0) *x = y;
  return count;
}

extern "C"
int pure_fscanf_double(FILE *fp, const char *format, double *x)
{
  int count = -1;
  fscanf(fp, myformat(format), x, &count);
  return count;
}

extern "C"
int pure_fscanf_string(FILE *fp, const char *format, const char *x)
{
  int count = -1;
  fscanf(fp, myformat(format), x, &count);
  return count;
}

extern "C"
int pure_fscanf_pointer(FILE *fp, const char *format, const void **x)
{
  int count = -1;
  fscanf(fp, myformat(format), x, &count);
  return count;
}

extern "C"
int pure_sscanf(const char *buf, const char *format)
{
  int count = -1;
  sscanf(buf, myformat(format), &count);
  return count;
}

extern "C"
int pure_sscanf_int(const char *buf, const char *format, int32_t *x)
{
  // wrap this up in case int on the target platform is not 32 bit
  int count = -1, y; sscanf(buf, myformat(format), &y, &count);
  if (count >= 0) *x = y;
  return count;
}

extern "C"
int pure_sscanf_double(const char *buf, const char *format, double *x)
{
  int count = -1;
  sscanf(buf, myformat(format), x, &count);
  return count;
}

extern "C"
int pure_sscanf_string(const char *buf, const char *format, char *x)
{
  int count = -1;
  sscanf(buf, myformat(format), x, &count);
  return count;
}

extern "C"
int pure_sscanf_pointer(const char *buf, const char *format, void **x)
{
  int count = -1;
  sscanf(buf, myformat(format), x, &count);
  return count;
}

#include <fnmatch.h>
#include <glob.h>

extern "C"
pure_expr *globlist(const glob_t *pglob)
{
  interpreter& interp = *interpreter::g_interp;
  pure_expr *x = pure_const(interp.symtab.nil_sym().f);
  int i = pglob->gl_pathc;
  while (--i >= 0) {
    pure_expr *f = pure_const(interp.symtab.cons_sym().f);
    pure_expr *y = pure_cstring_dup(pglob->gl_pathv[i]);
    x = pure_apply2(pure_apply2(f, y), x);
  }
  return x;
}

#include <sys/types.h>
#include <regex.h>

extern "C"
pure_expr *regmatches(const regex_t *preg, int flags)
{
  interpreter& interp = *interpreter::g_interp;
  int n = (flags&REG_NOSUB)?0:preg->re_nsub+1;
  regmatch_t *matches = 0;
  if (n > 0) matches = (regmatch_t*)malloc(n*sizeof(regmatch_t));
  pure_expr *f = pure_const(interp.symtab.pair_sym().f);
  pure_expr *x = pure_apply2(pure_apply2(f, pure_int(n)),
			     pure_pointer(matches));
  return x;
}

static int translate_pos(char *s, int p, int l)
{
  assert(p >= 0 && p <= l);
  char c = s[p]; s[p] = 0;
  char *t = toutf8(s); assert(t);
  int res = strlen(t);
  free(t); s[p] = c;
  return res;
}

extern "C"
pure_expr *reglist(const regex_t *preg, const char *s,
		   const regmatch_t *matches)
{
  interpreter& interp = *interpreter::g_interp;
  if (!matches) return pure_const(interp.symtab.void_sym().f);
  int n = preg->re_nsub+1;
  pure_expr *x = 0;
  int i = n;
  if (strcmp(default_encoding(), "UTF-8") == 0) {
    // Optimize for the case that the system encoding is utf-8. This should be
    // pretty much standard on Linux/Unix systems these days.
    while (--i >= 0) {
      pure_expr *f = pure_const(interp.symtab.pair_sym().f);
      pure_expr *y1 = pure_int(matches[i].rm_so);
      pure_expr *y2;
      if (matches[i].rm_so >= 0 && matches[i].rm_eo >= matches[i].rm_so) {
	size_t n = matches[i].rm_eo - matches[i].rm_so;
	char *buf = (char*)malloc(n+1); assert(buf);
	strncpy(buf, s+matches[i].rm_so, n); buf[n] = 0;
	y2 = pure_cstring(buf);
      } else
	y2 = pure_cstring_dup("");
      if (x)
	x = pure_apply2(pure_apply2(f, y2), x);
      else
	x = y2;
      x = pure_apply2(pure_apply2(f, y1), x);
    }
    return x;
  }
  // Translate the subject string to the system encoding. We also use this as
  // a work buffer to translate system encoding offsets to the utf-8 encoding
  // on the fly.
  char *t = fromutf8(s); assert(t);
  // Compute the position of the first match.
  assert(matches[0].rm_so >= 0);
  int l0 = strlen(t), p0 = matches[0].rm_so, q0 = translate_pos(t, p0, l0);
  char *u = t+p0;
  int l = l0-p0;
  while (--i >= 0) {
    int p = matches[i].rm_so, q;
    if (p < 0)
      q = -1;
    else {
      assert(p >= p0);
      q = q0 + translate_pos(u, p-p0, l);
    }
    pure_expr *f = pure_const(interp.symtab.pair_sym().f);
    pure_expr *y1 = pure_int(q);
    pure_expr *y2;
    if (q >= 0 && matches[i].rm_eo >= matches[i].rm_so) {
      size_t n = matches[i].rm_eo - matches[i].rm_so;
      char *buf = (char*)malloc(n+1); assert(buf);
      strncpy(buf, t+matches[i].rm_so, n); buf[n] = 0;
      y2 = pure_cstring(buf);
    } else
      y2 = pure_cstring_dup("");
    if (x)
      x = pure_apply2(pure_apply2(f, y2), x);
    else
      x = y2;
    x = pure_apply2(pure_apply2(f, y1), x);
  }
  free(t);
  return x;
}

static inline void
df(interpreter& interp, const char* s, pure_expr *x)
{
  try {
    interp.defn(s, x);
  } catch (err &e) {
    cerr << "warning: " << e.what() << endl;
  }
  MEMDEBUG_FREE(x)
}

static inline void
cdf(interpreter& interp, const char* s, pure_expr *x)
{
  try {
    interp.const_defn(s, x);
  } catch (err &e) {
    cerr << "warning: " << e.what() << endl;
  }
  pure_freenew(x);
}

extern "C"
void pure_sys_vars(void)
{
  interpreter& interp = *interpreter::g_interp;
  // standard I/O streams
  df(interp, "stdin",	pure_pointer(stdin));
  df(interp, "stdout",	pure_pointer(stdout));
  df(interp, "stderr",	pure_pointer(stderr));
  // memory sizes
  cdf(interp, "SIZEOF_BYTE",	pure_int(1));
  cdf(interp, "SIZEOF_SHORT",	pure_int(sizeof(short)));
  cdf(interp, "SIZEOF_INT",	pure_int(sizeof(int)));
  cdf(interp, "SIZEOF_LONG",	pure_int(sizeof(long)));
  cdf(interp, "SIZEOF_LONG_LONG",	pure_int(sizeof(long long)));
  cdf(interp, "SIZEOF_FLOAT",	pure_int(sizeof(float)));
  cdf(interp, "SIZEOF_DOUBLE",	pure_int(sizeof(double)));
#ifdef HAVE__COMPLEX_FLOAT
  cdf(interp, "SIZEOF_COMPLEX_FLOAT",	pure_int(sizeof(_Complex float)));
#endif
#ifdef HAVE__COMPLEX_DOUBLE
  cdf(interp, "SIZEOF_COMPLEX_DOUBLE",	pure_int(sizeof(_Complex double)));
#endif
  cdf(interp, "SIZEOF_POINTER",	pure_int(sizeof(void*)));
  // clock
  cdf(interp, "CLOCKS_PER_SEC",	pure_int(CLOCKS_PER_SEC));
  // fnmatch, glob
  cdf(interp, "FNM_NOESCAPE",	pure_int(FNM_NOESCAPE));
  cdf(interp, "FNM_PATHNAME",	pure_int(FNM_PATHNAME));
  cdf(interp, "FNM_PERIOD",	pure_int(FNM_PERIOD));
  cdf(interp, "FNM_CASEFOLD",	pure_int(FNM_CASEFOLD));
  cdf(interp, "GLOB_SIZE",	pure_int(sizeof(glob_t))); // not in POSIX
  cdf(interp, "GLOB_ERR",	pure_int(GLOB_ERR));
  cdf(interp, "GLOB_MARK",	pure_int(GLOB_MARK));
  cdf(interp, "GLOB_NOSORT",	pure_int(GLOB_NOSORT));
  cdf(interp, "GLOB_NOCHECK",	pure_int(GLOB_NOCHECK));
  cdf(interp, "GLOB_NOESCAPE",	pure_int(GLOB_NOESCAPE));
#ifndef __APPLE__
  cdf(interp, "GLOB_PERIOD",	pure_int(GLOB_PERIOD));
  cdf(interp, "GLOB_ONLYDIR",	pure_int(GLOB_ONLYDIR));
#endif
  cdf(interp, "GLOB_BRACE",	pure_int(GLOB_BRACE));
  cdf(interp, "GLOB_NOMAGIC",	pure_int(GLOB_NOMAGIC));
  cdf(interp, "GLOB_TILDE",	pure_int(GLOB_TILDE));
  // regex stuff
  cdf(interp, "REG_SIZE",	pure_int(sizeof(regex_t))); // not in POSIX
  cdf(interp, "REG_EXTENDED",	pure_int(REG_EXTENDED));
  cdf(interp, "REG_ICASE",	pure_int(REG_ICASE));
  cdf(interp, "REG_NOSUB",	pure_int(REG_NOSUB));
  cdf(interp, "REG_NEWLINE",	pure_int(REG_NEWLINE));
  cdf(interp, "REG_NOTBOL",	pure_int(REG_NOTBOL));
  cdf(interp, "REG_NOTEOL",	pure_int(REG_NOTEOL));
  // regcomp error codes
  cdf(interp, "REG_BADBR",	pure_int(REG_BADBR));
  cdf(interp, "REG_BADPAT",	pure_int(REG_BADPAT));
  cdf(interp, "REG_BADRPT",	pure_int(REG_BADRPT));
  cdf(interp, "REG_ECOLLATE",	pure_int(REG_ECOLLATE));
  cdf(interp, "REG_ECTYPE",	pure_int(REG_ECTYPE));
  cdf(interp, "REG_EESCAPE",	pure_int(REG_EESCAPE));
  cdf(interp, "REG_ESUBREG",	pure_int(REG_ESUBREG));
  cdf(interp, "REG_EBRACK",	pure_int(REG_EBRACK));
  cdf(interp, "REG_EPAREN",	pure_int(REG_EPAREN));
  cdf(interp, "REG_EBRACE",	pure_int(REG_EBRACE));
  cdf(interp, "REG_ERANGE",	pure_int(REG_ERANGE));
  cdf(interp, "REG_ESPACE",	pure_int(REG_ESPACE));
  // regexec error codes
  cdf(interp, "REG_NOMATCH",	pure_int(REG_NOMATCH));
  // signal actions
  cdf(interp, "SIG_TRAP",	pure_int(1));
  cdf(interp, "SIG_IGN",	pure_int(-1));
  cdf(interp, "SIG_DFL",	pure_int(0));
  // signals
#ifdef SIGHUP
  cdf(interp, "SIGHUP",		pure_int(SIGHUP));
#endif
#ifdef SIGINT
  cdf(interp, "SIGINT",		pure_int(SIGINT));
#endif
#ifdef SIGQUIT
  cdf(interp, "SIGQUIT",	pure_int(SIGQUIT));
#endif
#ifdef SIGILL
  cdf(interp, "SIGILL",		pure_int(SIGILL));
#endif
#ifdef SIGABRT
  cdf(interp, "SIGABRT",	pure_int(SIGABRT));
#endif
#ifdef SIGFPE
  cdf(interp, "SIGFPE",		pure_int(SIGFPE));
#endif
#ifdef SIGKILL
  cdf(interp, "SIGKILL",	pure_int(SIGKILL));
#endif
#ifdef SIGSEGV
  cdf(interp, "SIGSEGV",	pure_int(SIGSEGV));
#endif
#ifdef SIGPIPE
  cdf(interp, "SIGPIPE",	pure_int(SIGPIPE));
#endif
#ifdef SIGALRM
  cdf(interp, "SIGALRM",	pure_int(SIGALRM));
#endif
#ifdef SIGTERM
  cdf(interp, "SIGTERM",	pure_int(SIGTERM));
#endif
#ifdef SIGUSR1
  cdf(interp, "SIGUSR1",	pure_int(SIGUSR1));
#endif
#ifdef SIGUSR2
  cdf(interp, "SIGUSR2",	pure_int(SIGUSR2));
#endif
#ifdef SIGCHLD
  cdf(interp, "SIGCHLD",	pure_int(SIGCHLD));
#endif
#ifdef SIGCONT
  cdf(interp, "SIGCONT",	pure_int(SIGCONT));
#endif
#ifdef SIGSTOP
  cdf(interp, "SIGSTOP",	pure_int(SIGSTOP));
#endif
#ifdef SIGTSTP
  cdf(interp, "SIGTSTP",	pure_int(SIGTSTP));
#endif
#ifdef SIGTTIN
  cdf(interp, "SIGTTIN",	pure_int(SIGTTIN));
#endif
#ifdef SIGTTOU
  cdf(interp, "SIGTTOU",	pure_int(SIGTTOU));
#endif
  // setlocale
#ifdef LC_ALL
  cdf(interp, "LC_ALL",		pure_int(LC_ALL));
#endif
#ifdef LC_COLLATE
  cdf(interp, "LC_COLLATE",	pure_int(LC_COLLATE));
#endif
#ifdef LC_CTYPE
  cdf(interp, "LC_CTYPE",	pure_int(LC_CTYPE));
#endif
#ifdef LC_MESSAGES
  cdf(interp, "LC_MESSAGES",	pure_int(LC_MESSAGES));
#endif
#ifdef LC_MONETARY
  cdf(interp, "LC_MONETARY",	pure_int(LC_MONETARY));
#endif
#ifdef LC_NUMERIC
  cdf(interp, "LC_NUMERIC",	pure_int(LC_NUMERIC));
#endif
#ifdef LC_TIME
  cdf(interp, "LC_TIME",	pure_int(LC_TIME));
#endif
}
