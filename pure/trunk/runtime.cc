
#include "runtime.h"
#include "expr.hh"
#include "interpreter.hh"
#include "util.hh"
#include <readline/readline.h>
#include <readline/history.h>
#include <stdarg.h>
#include <iostream>
#include <sstream>

#include "funcall.h"

// Hook to report stack overflows and other kinds of hard errors.
#define checkstk(test) if (interpreter::brkflag)			\
    pure_throw(signal_exception(interpreter::brkflag));			\
  else if (interpreter::stackmax > 0 &&					\
      interpreter::stackdir*(&test - interpreter::baseptr)		\
      >= interpreter::stackmax)						\
    pure_throw(stack_exception())

// Debug expression allocations. Warns about expression memory leaks.
// NOTE: Bookkeeping starts and ends at each toplevel pure_invoke call.
// Enabling this code will make the interpreter *much* slower.
#if DEBUG>2
#if DEBUG>9 // enable this to print each and every expression (de)allocation
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
  }
}
#else
#define MEMDEBUG_NEW(x)
#define MEMDEBUG_FREE(x)
#define MEMDEBUG_INIT
#define MEMDEBUG_SUMMARY(ret)
#endif

// Debug shadow stack manipulations. SSTK_DEBUG=1 adds special code to verify
// the integrity of the shadow stack, any level >1 also prints pushes and pops
// of stack frames. NOTE: Enabling this code slows down the interpreter, and
// generates *lots* of debugging output for level >1.
#if DEBUG>2
#define SSTK_DEBUG 2
#else
#if DEBUG>1
#define SSTK_DEBUG 1
#else
#define SSTK_DEBUG 0
#endif
#endif

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
  if (x->data.clos->env) {
    for (size_t i = 0; i < x->data.clos->m; i++)
      pure_free(x->data.clos->env[i]);
    delete[] x->data.clos->env;
  }
  delete x->data.clos;
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
    switch (x->tag) {
    case EXPR::APP:
      y = x->data.x[0];
      assert(!x->xp);
      x->xp = xp; xp = x; x = y;
      goto loop;
    case EXPR::INT:
    case EXPR::DBL:
      // nothing to do
      break;
    case EXPR::BIGINT:
      mpz_clear(x->data.z);
      break;
    case EXPR::STR:
      free(x->data.s);
      break;
    case EXPR::PTR:
      // noop right now, should provide installable hook in the future
      break;
    default:
      assert(x->tag >= 0);
      if (x->data.clos) pure_free_clos(x);
      break;
    }
  }
  while (xp && x == xp->data.x[1]) {
    if (x->refc == 0) free_expr(x);
    x = xp; xp = x->xp;
  }
  if (x->refc == 0) free_expr(x);
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
  return pure_new_internal(pure_apply2(f, x));
}

static inline pure_expr* stack_exception()
{
  if (!interpreter::g_interp) return 0;
  return pure_new_internal
    (pure_const(interpreter::g_interp->symtab.segfault_sym().f));
}

extern "C"
pure_expr *pure_clos(bool local, bool thunked, int32_t tag, uint32_t n,
		     void *f, uint32_t m, /* m x pure_expr* */ ...)
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
pure_expr *pure_int(int32_t i)
{
  pure_expr *x = new_expr();
  x->tag = EXPR::INT;
  x->data.i = i;
  MEMDEBUG_NEW(x)
  return x;
}

static void make_bigint(mpz_t z, int32_t size, limb_t *limbs)
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
pure_expr *pure_bigint(int32_t size, limb_t *limbs)
{
  pure_expr *x = new_expr();
  x->tag = EXPR::BIGINT;
  make_bigint(x->data.z, size, limbs);
  MEMDEBUG_NEW(x)
  return x;
}

extern "C"
pure_expr *pure_mpz(mpz_t z)
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
int32_t pure_cmp_bigint(pure_expr *x, int32_t size, limb_t *limbs)
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

void pure_free_cstrings()
{
  for (list<char*>::iterator t = temps.begin(); t != temps.end(); t++)
    if (*t) free(*t);
  temps.clear();
}

void *pure_get_bigint(pure_expr *x)
{
  assert(x && x->tag == EXPR::BIGINT);
  return &x->data.z;
}

extern "C"
pure_expr *pure_call(pure_expr *x)
{
  char test;
  assert(x);
  if (x->tag >= 0 && x->data.clos && x->data.clos->n == 0) {
    void *fp = x->data.clos->fp;
#if DEBUG>1
    cerr << "pure_call: calling " << x << " -> " << fp << endl;
#endif
    assert(x->tag > 0 && x->refc > 0 && !x->data.clos->local);
    // parameterless call
    checkstk(test);
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

extern "C"
pure_expr *pure_apply(pure_expr *x, pure_expr *y)
{
  char test;
  assert(x && y && x->refc > 0 && y->refc > 0);
  // travel down the spine, count arguments
  pure_expr *f = x;
  uint32_t n = 0;
  while (f->tag == EXPR::APP) { f = f->data.x[0]; n++; }
  if (f->tag >= 0 && f->data.clos && !f->data.clos->thunked &&
      f->data.clos->n == n+1) {
    // saturated call; execute it now
    interpreter& interp = *interpreter::g_interp;
    void *fp = f->data.clos->fp;
    size_t i = 0, m = f->data.clos->m, k = n+1+(m>0?1:0);
    assert(k <= MAXARGS && "pure_apply: function call exceeds maximum #args");
    void **argv = (void**)alloca(k*sizeof(void*));
    assert(argv && "pure_apply: stack overflow");
    assert(f->data.clos->local || m == 0);
    pure_expr **env = 0;
    if (f->data.clos->local && m>0) {
      // add implicit environment parameter
      env = (pure_expr**)alloca(m*sizeof(pure_expr*));
      assert(env && "pure_apply: stack overflow");
      argv[i++] = env;
      for (size_t j = 0; j < m; j++) {
	assert(f->data.clos->env[j]->refc > 0);
	env[j] = f->data.clos->env[j]; env[j]->refc++;
      }
    }
    // collect arguments
    f = x;
    for (size_t j = 1; f->tag == EXPR::APP; j++, f = f->data.x[0]) {
      assert(f->data.x[1]->refc > 0);
      argv[i+n-j] = f->data.x[1]; f->data.x[1]->refc++;
    }
    i += n; argv[i++] = y;
    assert(i == k);
    pure_free_internal(x);
    // construct a stack frame
    {
      size_t sz = interp.sstk_sz;
      assert(k == n+1+(env?1:0));
      resize_sstk(interp.sstk, interp.sstk_cap, sz, n+m+2);
      pure_expr** sstk = interp.sstk;
      sstk[sz++] = 0;
      for (size_t j = env?1:0; j < k; j++)
	sstk[sz++] = (pure_expr*)argv[j];
      if (env) for (size_t j = 0; j < m; j++)
	sstk[sz++] = env[j];
#if SSTK_DEBUG>1
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
    cerr << "pure_apply: calling " << x << " (" << y << ") -> " << fp << endl;
#endif
    checkstk(test);
    funcall(fp, k, argv);
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
    if (e) {
      assert(e->refc > 0);
      pure_unref_internal(e);
    }
    longjmp(interp.estk.front().jmp, 1);
  }
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
    pure_expr **env = 0;
    if (x->data.clos->local && m>0) {
      // add implicit environment parameter
      env = (pure_expr**)alloca(m*sizeof(pure_expr*));
      assert(env && "pure_catch: stack overflow");
      for (size_t i = 0; i < m; i++) {
	assert(x->data.clos->env[i]->refc > 0);
	env[i] = x->data.clos->env[i]; env[i]->refc++;
      }
      // construct a stack frame
      size_t sz = interp.sstk_sz;;
      resize_sstk(interp.sstk, interp.sstk_cap, sz, m+1);
      pure_expr** sstk = interp.sstk;
      sstk[sz++] = 0;
      for (size_t i = 0; i < m; i++)
	sstk[sz++] = env[i];
#if SSTK_DEBUG>1
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
    pure_exception ex; ex.e = 0; ex.sz = interp.sstk_sz;
    interp.estk.push_front(ex);
    // Call the function now. Catch exceptions generated by the runtime.
    if (setjmp(interp.estk.front().jmp)) {
      // caught an exception
      size_t sz = interp.estk.front().sz;
      pure_expr *e = interp.estk.front().e;
      interp.estk.pop_front();
      // collect garbage
      pure_expr *tmps = interp.tmps;
      while (tmps) {
	pure_expr *next = tmps->xp;
	if (tmps != e) pure_freenew(tmps);
	tmps = next;
      }
      for (size_t i = interp.sstk_sz; i-- > sz; )
	if (interp.sstk[i] && interp.sstk[i]->refc > 0)
	  pure_free_internal(interp.sstk[i]);
      interp.sstk_sz = sz;
      if (!e) e = pure_const(interp.symtab.void_sym().f);
      assert(e);
      h->refc--;
      pure_expr *res = pure_apply2(h, e);
      assert(res);
      res->refc++;
      pure_free_internal(x);
      pure_unref_internal(res);
      return res;
    } else {
      pure_expr *res;
      if (env)
	// pass environment
	res = ((pure_expr*(*)(void*))fp)(env);
      else
	// parameterless call
	res = ((pure_expr*(*)())fp)();
      // normal return
      interp.estk.pop_front();
#if DEBUG>1
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
pure_expr *pure_invoke(void *f, pure_expr*& e)
{
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
    // collect garbage
    pure_expr *tmps = interp.tmps;
    while (tmps) {
      pure_expr *next = tmps->xp;
      if (tmps != e) pure_freenew(tmps);
      tmps = next;
    }
    for (size_t i = interp.sstk_sz; i-- > sz; )
      if (interp.sstk[i] && interp.sstk[i]->refc > 0)
	pure_free_internal(interp.sstk[i]);
    interp.sstk_sz = sz;
    MEMDEBUG_SUMMARY(e)
    return 0;
  } else {
    pure_expr *res = fp();
    // normal return
    interp.estk.pop_front();
    MEMDEBUG_SUMMARY(res)
#if DEBUG>1
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
void pure_push_args(uint32_t n, ...)
{
  va_list ap;
  interpreter& interp = *interpreter::g_interp;
  size_t sz = interp.sstk_sz;
  resize_sstk(interp.sstk, interp.sstk_cap, sz, n+1);
  pure_expr** sstk = interp.sstk;
  // mark the beginning of this frame
  sstk[sz++] = 0;
  va_start(ap, n);
  while (n-- > 0) {
    pure_expr *x = va_arg(ap, pure_expr*);
    sstk[sz++] = x;
    if (x->refc > 0)
      x->refc++;
    else
      pure_new_internal(x);
  };
  va_end(ap);
#if SSTK_DEBUG>1
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
void pure_pop_args(pure_expr *x, uint32_t n, ...)
{
  va_list ap;
  interpreter& interp = *interpreter::g_interp;
#if SSTK_DEBUG
  pure_expr **sstk = interp.sstk;
  pure_expr **y = (pure_expr**)alloca(n*sizeof(pure_expr*));
  size_t i, sz = interp.sstk_sz, oldsz = sz;
  while (sz > 0 && sstk[--sz]) ;
  assert(sz < oldsz && !sstk[sz]);
  if (x) x->refc++;
  va_start(ap, n);
  for (i = 0; i < n; i++) {
    pure_expr *x = va_arg(ap, pure_expr*);
    y[i] = x;
  };
  va_end(ap);
  if (oldsz-sz-1 != n) goto error;
  for (i = 0; i < n; i++) {
    pure_expr *x = y[i];
    if (sstk[sz+i+1] != x) goto error;
  }
  interp.sstk_sz = sz;
#if SSTK_DEBUG>1
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
  for (size_t i = 0; i < n; i++) {
    pure_expr *x = y[i];
    if (x->refc > 1)
      x->refc--;
    else
      pure_free_internal(x);
  }
  return;
 error:
  cerr << "ERROR: can't find stack frame to be popped.\n";
  cerr << "++ stack: (oldsz = " << oldsz << ")\n";
  for (size_t i = 0; i < oldsz; i++) {
    pure_expr *x = sstk[i];
    if (x)
      cerr << i << ": " << (void*)x << ": " << x << endl;
    else
      cerr << i << ": " << "** frame **\n";
  }
  cerr << "++ pop:\n";
  for (size_t i = 0; i < n; i++) {
    pure_expr *x = y[i];
    cerr << i << ": " << (void*)x << ": " << x << endl;
  }
  abort();
#else
  // This doesn't verify the integrity of the shadow stack and is *much*
  // faster.
  interp.sstk_sz -= n+1;
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
#endif
}

extern "C"
void pure_pop_tail_args(pure_expr *x, uint32_t n, ...)
{
  va_list ap;
  interpreter& interp = *interpreter::g_interp;
#if SSTK_DEBUG
  pure_expr **sstk = interp.sstk;
  pure_expr **y = (pure_expr**)alloca(n*sizeof(pure_expr*));
  size_t i, sz = interp.sstk_sz, lastsz, oldsz = sz;
  while (sz > 0 && sstk[--sz]) ;
  assert(sz < oldsz && !sstk[sz]);
  lastsz = sz;
  while (sz > 0 && sstk[--sz]) ;
  assert(sz < lastsz && !sstk[sz]);
  if (x) x->refc++;
  va_start(ap, n);
  for (i = 0; i < n; i++) {
    pure_expr *x = va_arg(ap, pure_expr*);
    y[i] = x;
  };
  va_end(ap);
  if (lastsz-sz-1 != n) goto error;
  for (i = 0; i < n; i++) {
    pure_expr *x = y[i];
    if (sstk[sz+i+1] != x) goto error;
  }
#if SSTK_DEBUG>1
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
  memmove(sstk+sz, sstk+lastsz, (oldsz-lastsz)*sizeof(pure_expr*));
  oldsz -= n+1;
  interp.sstk_sz = oldsz;
#if SSTK_DEBUG>1
  cerr << "++ new stack: (newsz = " << oldsz << ")\n";
  for (size_t i = 0; i < oldsz; i++) {
    pure_expr *x = sstk[i];
    if (x)
      cerr << i << ": " << (void*)x << ": " << x << endl;
    else
      cerr << i << ": " << "** frame **\n";
  }
#endif
  for (size_t i = 0; i < n; i++) {
    pure_expr *x = y[i];
    if (x->refc > 1)
      x->refc--;
    else
      pure_free_internal(x);
  }
  return;
 error:
  cerr << "ERROR: can't find stack frame to be popped.\n";
  cerr << "++ stack: (oldsz = " << oldsz << ", lastsz = " << lastsz << ")\n";
  for (size_t i = 0; i < oldsz; i++) {
    pure_expr *x = sstk[i];
    if (x)
      cerr << i << ": " << (void*)x << ": " << x << endl;
    else
      cerr << i << ": " << "** frame **\n";
  }
  cerr << "++ pop:\n";
  for (size_t i = 0; i < n; i++) {
    pure_expr *x = y[i];
    cerr << i << ": " << (void*)x << ": " << x << endl;
  }
  abort();
#else
  // This doesn't verify the integrity of the shadow stack and is *much*
  // faster.
  pure_expr **sstk = interp.sstk;
  size_t lastsz = interp.sstk_sz, oldsz = lastsz;
  while (lastsz > 0 && sstk[--lastsz]) ;
  memmove(sstk+lastsz-n-1, sstk+lastsz, (oldsz-lastsz)*sizeof(pure_expr*));
  interp.sstk_sz -= n+1;
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
  switch (x->tag) {
  case EXPR::INT:	return x;
  case EXPR::BIGINT:	return pure_int(mpz_get_ui(x->data.z));
  case EXPR::DBL:	return pure_int((int32_t)x->data.d);
    // Must cast to 64 bit here first, since on 64 bit systems g++ gives an
    // error when directly casting a 64 bit pointer to a 32 bit integer.
  case EXPR::PTR:	return pure_int((uint32_t)(uint64_t)x->data.p);
  default:		return 0;
  }
}

extern "C"
pure_expr *pure_dblval(pure_expr *x)
{
  assert(x);
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
  switch (x->tag) {
  case EXPR::PTR:	return x;
  case EXPR::STR:	return pure_pointer(x->data.s);
  case EXPR::INT:	return pure_pointer((void*)x->data.i);
  case EXPR::BIGINT:
    if (sizeof(mp_limb_t) == 8)
      return pure_pointer((void*)x->data.z->_mp_d[0]);
    else if (sizeof(void*) == 4)
      return pure_pointer((void*)mpz_get_ui(x->data.z));
    else {
      uint64_t u = x->data.z->_mp_d[0]+(((uint64_t)x->data.z->_mp_d[1])<<32);
      return pure_pointer((void*)u);
    }
  default:		return 0;
  }
}

static pure_expr *pointer_to_bigint(void *p)
{
  if (sizeof(mp_limb_t) == 8) {
    // In this case the pointer value ought to fit into a single limb.
    limb_t u[1] = { (uint64_t)p };
    return pure_bigint(1, u);
  }
  // 4 byte limbs.
  if (sizeof(void*) == 4) {
    // 4 byte pointers. Note that we still cast to 64 bit first, since
    // otherwise the code will give an error on 64 bit systems.
    limb_t u[1] = { (uint32_t)(uint64_t)p };
    return pure_bigint(1, u);
  } else {
    // 8 byte pointers, put least significant word in the first limb.
    assert(sizeof(void*) == 8);
    limb_t u[2] = { (uint32_t)(uint64_t)p, (uint32_t)(((uint64_t)p)>>32) };
    return pure_bigint(2, u);
  }
}

extern "C"
pure_expr *pure_bigintval(pure_expr *x)
{
  assert(x);
  if (x->tag == EXPR::BIGINT)
    return x;
  else if (x->tag == EXPR::PTR)
    return pointer_to_bigint(x->data.p);
  else if (x->tag != EXPR::INT && x->tag == EXPR::DBL)
    return 0;
  pure_expr *y = pure_bigint(0, 0);
  mpz_t& z = y->data.z;
  if (x->tag == EXPR::INT)
    mpz_set_si(z, x->data.i);
  else if (x->tag == EXPR::DBL)
    mpz_set_d(z, x->data.d);
  return y;
}

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

extern "C"
pure_expr *bigint_div(mpz_t x, mpz_t y)
{
  pure_expr *u = pure_bigint(0, 0);
  mpz_t& z = u->data.z;
  mpz_tdiv_q(z, x, y);
  return u;
}

extern "C"
pure_expr *bigint_mod(mpz_t x, mpz_t y)
{
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

static inline bool is_nil(pure_expr *xs)
{
  interpreter& interp = *interpreter::g_interp;
  return xs->tag == interp.symtab.nil_sym().f;
}

static inline bool is_cons(pure_expr *xs, pure_expr*& y, pure_expr*& ys)
{
  interpreter& interp = *interpreter::g_interp;
  if (xs->tag == EXPR::APP && xs->data.x[0]->tag == EXPR::APP &&
      xs->data.x[0]->data.x[0]->tag == interp.symtab.cons_sym().f) {
    y = xs->data.x[0]->data.x[1];
    ys = xs->data.x[1];
    return true;
  } else
    return false;
}

extern "C"
pure_expr *string_concat_list(pure_expr *xs)
{
  // linear-time concatenation of a list of strings
  // calculate the size of the result string
  pure_expr *ys = xs, *z, *zs;
  size_t n = 0;
  while (is_cons(ys, z, zs) && z->tag == EXPR::STR) {
    n += strlen(z->data.s);
    ys = zs;
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
pure_expr *str(const pure_expr *x)
{
  assert(x);
  ostringstream os;
  try {
    os << x;
    return pure_string_dup(os.str().c_str());
  } catch (err &e) {
    return 0;
  }
}

extern "C"
pure_expr *eval(const char *s)
{
  assert(s);
  interpreter& interp = *interpreter::g_interp;
  pure_expr *res = interp.runstr(string(s)+";");
  if (res) pure_unref_internal(res);
  return res;
}

extern "C"
bool same(const pure_expr *x, const pure_expr *y)
{
  char test;
  if (x == y)
    return 1;
  else if (x->tag >= 0 && y->tag >= 0)
    if (x->data.clos && y->data.clos)
      return x->tag == y->tag && x->data.clos->fp == y->data.clos->fp;
    else
      return x->tag == y->tag && x->data.clos == y->data.clos;
  else if (x->tag != y->tag)
    return 0;
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
      return x->data.d == y->data.d;
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
  return (x->tag == 0 && x->data.clos);
}

extern "C"
bool varp(const pure_expr *x)
{
  return (x->tag > 0 && !x->data.clos);
}

extern "C"
bool applp(const pure_expr *x)
{
  return (x->tag == EXPR::APP);
}

extern "C"
pure_expr *fun(const pure_expr *x)
{
  if (x->tag == EXPR::APP)
    return x->data.x[0];
  else
    return 0;
}

extern "C"
pure_expr *arg(const pure_expr *x)
{
  if (x->tag == EXPR::APP)
    return x->data.x[1];
  else
    return 0;
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

extern "C"
int pure_fscanf(FILE *fp, const char *format)
{
  return fscanf(fp, format);
}

extern "C"
int pure_fscanf_int(FILE *fp, const char *format, int32_t *x)
{
  return fscanf(fp, format, x);
}

extern "C"
int pure_fscanf_double(FILE *fp, const char *format, double *x)
{
  return fscanf(fp, format, x);
}

extern "C"
int pure_fscanf_string(FILE *fp, const char *format, const char *x)
{
  return fscanf(fp, format, x);
}

extern "C"
int pure_fscanf_pointer(FILE *fp, const char *format, const void **x)
{
  return fscanf(fp, format, x);
}

#define myformat(format) sscanf_format((char*)alloca(strlen(format)+3), format)

static inline char *sscanf_format(char *buf, const char *format)
{
  strcpy(buf, format); strcat(buf, "%n");
  return buf;
}

extern "C"
int pure_sscanf(const char *buf, const char *format)
{
  int count, res = sscanf(buf, myformat(format), &count);
  return (res >= 0)?count:-1;
}

extern "C"
int pure_sscanf_int(const char *buf, const char *format, int32_t *x)
{
  // wrap this up in case int on the target platform is not 32 bit
  int count, y, res = sscanf(buf, myformat(format), &y, &count);
  *x = y;
  return (res > 0)?count:-1;
}

extern "C"
int pure_sscanf_double(const char *buf, const char *format, double *x)
{
  int count, res = sscanf(buf, myformat(format), x, &count);
  return (res > 0)?count:-1;
}

extern "C"
int pure_sscanf_string(const char *buf, const char *format, char *x)
{
  int count, res = sscanf(buf, myformat(format), x, &count);
  return (res > 0)?count:-1;
}

extern "C"
int pure_sscanf_pointer(const char *buf, const char *format, void **x)
{
  int count, res = sscanf(buf, myformat(format), x, &count);
  return (res > 0)?count:-1;
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

extern "C"
void pure_sys_vars(void)
{
  interpreter& interp = *interpreter::g_interp;
  // standard I/O streams
  interp.defn("stdin",		pure_pointer(stdin));
  interp.defn("stdout",	pure_pointer(stdout));
  interp.defn("stderr",	pure_pointer(stderr));
  // fnmatch, glob
  interp.defn("FNM_NOESCAPE",	pure_int(FNM_NOESCAPE));
  interp.defn("FNM_PATHNAME",	pure_int(FNM_PATHNAME));
  interp.defn("FNM_PERIOD",	pure_int(FNM_PERIOD));
  interp.defn("FNM_CASEFOLD",	pure_int(FNM_CASEFOLD));
  interp.defn("GLOB_SIZE",	pure_int(sizeof(glob_t))); // not in POSIX
  interp.defn("GLOB_ERR",	pure_int(GLOB_ERR));
  interp.defn("GLOB_MARK",	pure_int(GLOB_MARK));
  interp.defn("GLOB_NOSORT",	pure_int(GLOB_NOSORT));
  interp.defn("GLOB_NOCHECK",	pure_int(GLOB_NOCHECK));
  interp.defn("GLOB_NOESCAPE",	pure_int(GLOB_NOESCAPE));
#ifndef __APPLE__
  interp.defn("GLOB_PERIOD",	pure_int(GLOB_PERIOD));
  interp.defn("GLOB_ONLYDIR",	pure_int(GLOB_ONLYDIR));
#endif
  interp.defn("GLOB_BRACE",	pure_int(GLOB_BRACE));
  interp.defn("GLOB_NOMAGIC",	pure_int(GLOB_NOMAGIC));
  interp.defn("GLOB_TILDE",	pure_int(GLOB_TILDE));
  // regex stuff
  interp.defn("REG_SIZE",	pure_int(sizeof(regex_t))); // not in POSIX
  interp.defn("REG_EXTENDED",	pure_int(REG_EXTENDED));
  interp.defn("REG_ICASE",	pure_int(REG_ICASE));
  interp.defn("REG_NOSUB",	pure_int(REG_NOSUB));
  interp.defn("REG_NEWLINE",	pure_int(REG_NEWLINE));
  interp.defn("REG_NOTBOL",	pure_int(REG_NOTBOL));
  interp.defn("REG_NOTEOL",	pure_int(REG_NOTEOL));
  // regcomp error codes
  interp.defn("REG_BADBR",	pure_int(REG_BADBR));
  interp.defn("REG_BADPAT",	pure_int(REG_BADPAT));
  interp.defn("REG_BADRPT",	pure_int(REG_BADRPT));
  interp.defn("REG_ECOLLATE",	pure_int(REG_ECOLLATE));
  interp.defn("REG_ECTYPE",	pure_int(REG_ECTYPE));
  interp.defn("REG_EESCAPE",	pure_int(REG_EESCAPE));
  interp.defn("REG_ESUBREG",	pure_int(REG_ESUBREG));
  interp.defn("REG_EBRACK",	pure_int(REG_EBRACK));
  interp.defn("REG_EPAREN",	pure_int(REG_EPAREN));
  interp.defn("REG_EBRACE",	pure_int(REG_EBRACE));
  interp.defn("REG_ERANGE",	pure_int(REG_ERANGE));
  interp.defn("REG_ESPACE",	pure_int(REG_ESPACE));
  // regexec error codes
  interp.defn("REG_NOMATCH",	pure_int(REG_NOMATCH));
  interp.defn("REG_ESPACE",	pure_int(REG_ESPACE));
}
