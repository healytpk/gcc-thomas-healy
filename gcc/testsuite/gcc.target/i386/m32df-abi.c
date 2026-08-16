/* Verify the -m32df type widths: data pointers are 32-bit while function
   pointers are 64-bit, so uintptr_t is too narrow to round-trip a function
   pointer and uintfptr_t is needed.  */
/* { dg-do compile { target { x86_64-*-* } } } */
/* { dg-options "-m32df" } */

_Static_assert (sizeof (void *) == 4, "data pointer is 32-bit");
_Static_assert (sizeof (int *) == 4, "object pointer is 32-bit");
_Static_assert (sizeof (long) == 4, "ILP32: long is 32-bit");
_Static_assert (sizeof (void (*) (void)) == 8, "function pointer is 64-bit");
_Static_assert (sizeof (__UINTPTR_TYPE__) == 4, "uintptr_t is 32-bit");
_Static_assert (sizeof (__UINTFPTR_TYPE__) == 8, "uintfptr_t is 64-bit");
_Static_assert (sizeof (__INTFPTR_TYPE__) == 8, "intfptr_t is 64-bit");
_Static_assert (sizeof (void *) < sizeof (void (*) (void)),
		"the point: a function pointer does not fit in a void *");
