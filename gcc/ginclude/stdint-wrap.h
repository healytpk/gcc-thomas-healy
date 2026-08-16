#ifndef _GCC_WRAP_STDINT_H
#if __STDC_HOSTED__
# if defined __cplusplus && __cplusplus >= 201103L
#  undef __STDC_LIMIT_MACROS
#  define __STDC_LIMIT_MACROS
#  undef __STDC_CONSTANT_MACROS
#  define __STDC_CONSTANT_MACROS
# endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // include_next
# include_next <stdint.h>
#pragma GCC diagnostic pop
/* Integer types capable of holding function pointers.  [u]intptr_t is
   specified for *object* pointers only, so on a target whose function
   pointers are wider it cannot round-trip one; these types can.  The C
   library's <stdint.h> does not define them, so they are added here.  Where
   the two widths agree they are the same types.  */
# ifdef __INTFPTR_TYPE__
typedef __INTFPTR_TYPE__ intfptr_t;
# endif
# ifdef __UINTFPTR_TYPE__
typedef __UINTFPTR_TYPE__ uintfptr_t;
# endif
/* Integer types capable of holding either kind of address; see stdint-gcc.h.  */
# ifdef __INTP_TYPE__
typedef __INTP_TYPE__ intp_t;
# endif
# ifdef __UINTP_TYPE__
typedef __UINTP_TYPE__ uintp_t;
# endif
#else
# include "stdint-gcc.h"
#endif
#define _GCC_WRAP_STDINT_H
#endif
