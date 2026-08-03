/* Minimal shims so a `ta_codegen/input/<name>/<name>.c` body compiles
 * standalone, out of the ta-lib tree.  Only the tokens those bodies use.
 */
#ifndef H147_SHIM_H
#define H147_SHIM_H

#include <stdlib.h>

typedef int TA_RetCode;
#define TA_SUCCESS    0
#define TA_ALLOC_ERR  2

/* Same expansion as src/ta_func/ta_utility.h for clang/gcc. */
#if defined(__clang__)
#  define TA_UNROLL_STR1(x) #x
#  define TA_UNROLL_STR(x)  TA_UNROLL_STR1(x)
#  define TA_UNROLL(n)      _Pragma(TA_UNROLL_STR(clang loop unroll_count(n)))
#elif defined(__GNUC__)
#  define TA_UNROLL_STR1(x) #x
#  define TA_UNROLL_STR(x)  TA_UNROLL_STR1(x)
#  define TA_UNROLL(n)      _Pragma(TA_UNROLL_STR(GCC unroll n))
#else
#  define TA_UNROLL(n)
#endif

#endif
