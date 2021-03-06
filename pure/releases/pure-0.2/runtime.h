#ifndef RUNTIME_H
#define RUNTIME_H

/* The Pure runtime interface. */

#include <stdint.h>
#include <setjmp.h>
#include <gmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Our "limb" type. Used to pass bigint constants to the runtime. */
#ifdef _LONG_LONG_LIMB // try to match what GMP uses
typedef uint64_t limb_t;
#else
typedef uint32_t limb_t;
#endif

/* Closure data. This is a bit on the heavy side, so expressions which need
   it (i.e., functions) refer to this extra data via an allocated pointer. */

typedef struct {
  void *fp;			// pointer to executable code
  uint32_t n, m;		// number of arguments and environment size
  struct _pure_expr **env;	// captured environment (if m>0, 0 otherwise)
  bool local;			// local function?
  bool thunked;			// thunked closure? (kept unevaluated)
} pure_closure;

/* The runtime expression data structure. Keep this lean and mean. */

typedef struct _pure_expr {
  /* Public fields, these *must* be layed out exactly as indicated.
     The JIT depends on it! */
  int32_t tag; // type tag or symbol, see expr.hh for possible values
  union {
    struct _pure_expr *x[2];	// application arguments (EXPR::APP)
    int32_t i;			// integer (EXPR::INT)
    mpz_t z;			// GMP bigint (EXPR::BIGINT)
    double d;			// double (EXPR::DBL)
    char *s;			// C string (EXPR::STR)
    void *p;			// generic pointer (EXPR::PTR)
    pure_closure *clos;		// closure (0 if none)
  } data;
  /* Internal fields (DO NOT TOUCH). The JIT doesn't care about these. */
  uint32_t refc;		// reference counter
  struct _pure_expr *xp;	// freelist pointer
} pure_expr;

/* Blocks of expression memory allocated in one chunk. */

#define MEMSIZE 0x20000 // 128K

typedef struct _pure_mem {
  struct _pure_mem *next;	// link to next block
  pure_expr *p;			// pointer to last used expression
  pure_expr x[MEMSIZE];		// expression data
} pure_mem;

/* Expression constructors. */

pure_expr *pure_clos(bool local, bool thunked, int32_t tag, uint32_t n,
		     void *f, uint32_t m, /* m x pure_expr* */ ...);
pure_expr *pure_const(int32_t tag);
pure_expr *pure_int(int32_t i);
pure_expr *pure_bigint(int32_t size, limb_t *limbs);
pure_expr *pure_mpz(mpz_t z);
pure_expr *pure_double(double d);
pure_expr *pure_pointer(void *p);

/* String constructors. There are four variations of these, depending on
   whether the original string is already in utf-8 (_string routines) or in
   the system encoding (_cstring), and whether the string should be copied
   (_dup suffix) or whether Pure takes ownership of the string (no _dup
   suffix). All these routines also handle the case that the given string is a
   null pointer and will then return the appropriate Pure pointer expression
   instead. */

pure_expr *pure_string_dup(const char *s);
pure_expr *pure_cstring_dup(const char *s);
pure_expr *pure_string(char *s);
pure_expr *pure_cstring(char *s);

/* Compare a bigint or string expression against a constant value. This is
   used by the pattern matching code. */

int32_t pure_cmp_bigint(pure_expr *x, int32_t size, limb_t *limbs);
int32_t pure_cmp_string(pure_expr *x, const char *s);

/* Get the string value of a string expression in the system encoding. Each
   call returns a new string, pure_free_cstrings() frees the temporary
   storage. This is only to be used internally, to unbox string arguments in
   the C interface. */

char *pure_get_cstring(pure_expr *x);
void pure_free_cstrings();

/* Get a pointer to the mpz_t value of a bigint expression which can be passed
   to the GMP routines. This is used to unbox bigint arguments and map them to
   void* in the C interface. */

void *pure_get_bigint(pure_expr *x);

/* Execute a closure. If the given expression x (or x y in the case of
   pure_apply) is a parameterless closure (or a saturated application of a
   closure), call it with the appropriate parameters and environment, if
   any. Otherwise just return x (or the literal application x y). */

pure_expr *pure_call(pure_expr *x);
pure_expr *pure_apply(pure_expr *x, pure_expr *y);

/* Exception handling stuff. */

typedef struct { jmp_buf jmp; pure_expr* e; } pure_exception;

/* Throw the given expression (which may also be null) as an exception. */

void pure_throw(pure_expr* e);

/* Execute a parameterless fbox x and return its result. If an exception
   occurs while x is executed, apply h to the value of the exception
   instead. */

pure_expr *pure_catch(pure_expr *h, pure_expr *x);

/* Run a Pure function and catch exceptions. If everything goes normal,
   pure_invoke returns the return value of the executed function. Otherwise it
   returns 0 and sets e to the exception value, as given by pure_throw().
   XXXFIXME: Only supports parameterless functions right now. */

pure_expr *pure_invoke(void *f, pure_expr*& e);

/* Count a new reference to an expression. This should be called whenever you
   want to store an expression somewhere, in order to prevent it from being
   garbage-collected. */

pure_expr *pure_new(pure_expr *x);

/* Drop a reference to an expression. This will cause the expression to be
   garbage-collected when it is no longer needed. */

void pure_free(pure_expr *x);

/* Count a reference and then immediately drop it. This is useful to collect
   temporaries which are not referenced yet. */

void pure_freenew(pure_expr *x);

/* Increment and decrement the reference counter. This can be used to
   temporarily protect an expression from being garbage-collected. It doesn't
   actually change the status of the expression and does not collect it. */

void pure_ref(pure_expr *x);
void pure_unref(pure_expr *x);

/* Count references on a given collection of arguments in preparation for a
   function call. Terminate the list of arguments with a null pointer. */

void pure_new_args(pure_expr *x, ...);

/* Collect call data. This is used to free arguments end environment when
   returning from a function. The first argument is the return value. If not
   null, an extra reference is temporarily counted on this expression so that
   it doesn't get freed if the return value happens to be a (subterm of an)
   argument or environment expression. (It's the caller's duty to call
   pure_unref later.) The remaining parameters is the list of expression
   pointers to be collected, terminated with a null pointer. */

void pure_free_args(pure_expr *x, ...);

/* Debugging support. Preliminary. */

void pure_debug(int32_t tag, const char *format, ...);

/* Supplementary routines. These are used in the standard library. */

/* Conversions between numeric and pointer types. The input argument must be
   an expression denoting an int, double, bigint or pointer value. The numeric
   value of a pointer is its address, cast to a suitably large integer type,
   which can be converted to/from an integer, but not a double value. Strings
   can be converted to pointers as well, but not the other way round. */

pure_expr *pure_intval(pure_expr *x);
pure_expr *pure_dblval(pure_expr *x);
pure_expr *pure_bigintval(pure_expr *x);
pure_expr *pure_pointerval(pure_expr *x);

/* Construct a "byte string" from a string. The result is a raw pointer object
   pointing to the converted string. The original string is copied (and, in
   the case of pure_byte_cstring, converted to the system encoding). The
   resulting byte string is a malloc'ed pointer which can be used like a C
   char* (using pointer arithmetic etc.; the usual caveats apply), and has to
   be freed by the caller when no longer needed. */

pure_expr *pure_byte_string(const char *s);
pure_expr *pure_byte_cstring(const char *s);

/* Bigint arithmetic. */

pure_expr *bigint_neg(mpz_t x);

pure_expr *bigint_add(mpz_t x, mpz_t y);
pure_expr *bigint_sub(mpz_t x, mpz_t y);
pure_expr *bigint_mul(mpz_t x, mpz_t y);
pure_expr *bigint_div(mpz_t x, mpz_t y);
pure_expr *bigint_mod(mpz_t x, mpz_t y);
pure_expr *bigint_pow(mpz_t x, uint32_t y);
pure_expr *bigint_shl(mpz_t x, int32_t y);
pure_expr *bigint_shr(mpz_t x, int32_t y);

pure_expr *bigint_not(mpz_t x);

pure_expr *bigint_and(mpz_t x, mpz_t y);
pure_expr *bigint_or(mpz_t x, mpz_t y);

int32_t bigint_cmp(mpz_t x, mpz_t y);

/* String operations. In difference to the string operations from the C
   library, these assume a utf-8 encoded string and will count and index
   individual characters accordingly. */

bool string_null(const char *s);
uint32_t string_size(const char *s);
pure_expr *string_char_at(const char *s, uint32_t n);

/* Note that indexing an utf-8 string takes linear time and is thus slow. As a
   remedy, we offer the following linear-time operation which converts an
   entire string to a list of its utf-8 characters in one go. */

pure_expr *string_chars(const char *s);

/* Concatenation, substrings and finding a substring in a string. */

pure_expr *string_concat(const char* s, const char *t);
pure_expr *string_concat_list(pure_expr *xs);
pure_expr *string_substr(const char* s, uint32_t pos, uint32_t size);
int32_t string_index(const char* s, const char *t);

/* Conversions between utf-8 characters and numbers. These convert a Unicode
   character code (code point) to the corresponding utf-8 character, and vice
   versa. */

pure_expr *string_chr(uint32_t n);
pure_expr *string_ord(const char *c);

/* Convert a Pure expression to a string and vice versa. Note that eval() will
   actually parse and execute any Pure source, so it can be used, e.g., to add
   new rules to the executing program at runtime. */

pure_expr *str(const pure_expr *x);
pure_expr *eval(const char *s);

/* Check whether two objects are the "same" (syntactically). */

bool same(const pure_expr *x, const pure_expr *y);

/* Check whether an object is a function application, and return the function
   and the argument of an application. Note that these operations can't be
   defined in Pure because of the "head is function" rule which means that in
   a pattern of the form f x, f is always a literal function symbol and not a
   variable. */

bool applp(const pure_expr *x);
pure_expr *fun(const pure_expr *x);
pure_expr *arg(const pure_expr *x);

/* Direct memory accesses. */

int32_t pointer_get_byte(void *ptr);
int32_t pointer_get_int(void *ptr);
double pointer_get_double(void *ptr);
char *pointer_get_string(void *ptr);
void *pointer_get_pointer(void *ptr);

void pointer_put_byte(void *ptr, int32_t x);
void pointer_put_int(void *ptr, int32_t x);
void pointer_put_double(void *ptr, double x);
void pointer_put_string(void *ptr, const char *x);
void pointer_put_pointer(void *ptr, void *x);

/* Initialize a bunch of variables with useful system constants. */

void pure_sys_vars(void);

/* errno access. */

int pure_errno(void);
void pure_set_errno(int value);

/* printf/scanf support. Since we don't support calling C vararg functions
   from Pure right now, these little wrappers are provided to process at most
   one value at a time. It is the responsibility of the caller that the
   provided parameters match up with the format specifiers. */

#include <stdio.h>

int pure_fprintf(FILE *fp, const char *format);
int pure_fprintf_int(FILE *fp, const char *format, int32_t x);
int pure_fprintf_double(FILE *fp, const char *format, double x);
int pure_fprintf_string(FILE *fp, const char *format, const char *x);
int pure_fprintf_pointer(FILE *fp, const char *format, const void *x);

int pure_snprintf(char *buf, size_t size, const char *format);
int pure_snprintf_int(char *buf, size_t size, const char *format, int x);
int pure_snprintf_double(char *buf, size_t size, const char *format, double x);
int pure_snprintf_string(char *buf, size_t size, const char *format, const char *x);
int pure_snprintf_pointer(char *buf, size_t size, const char *format, const void *x);

int pure_fscanf(FILE *fp, const char *format);
int pure_fscanf_int(FILE *fp, const char *format, int32_t *x);
int pure_fscanf_double(FILE *fp, const char *format, double *x);
int pure_fscanf_string(FILE *fp, const char *format, const char *x);
int pure_fscanf_pointer(FILE *fp, const char *format, const void **x);

int pure_sscanf(const char *buf, const char *format);
int pure_sscanf_int(const char *buf, const char *format, int32_t *x);
int pure_sscanf_double(const char *buf, const char *format, double *x);
int pure_sscanf_string(const char *buf, const char *format, char *x);
int pure_sscanf_pointer(const char *buf, const char *format, void **x);

/* glob(3) support. */

#include <glob.h>

/* Decode the result of glob into a Pure list. */
pure_expr *globlist(const glob_t *pglob);

/* regexec/regcomp(3) support. */

#include <sys/types.h>
#include <regex.h>

/* Return the number of subre's and allocate storage for the matches. */
pure_expr *regmatches(const regex_t *preg, int flags);

/* Decode the result of regexec into a list of matches. */
pure_expr *reglist(const regex_t *preg, const char *s,
		   const regmatch_t *matches);

#ifdef __cplusplus
}
#endif

#endif // ! RUNTIME_H
