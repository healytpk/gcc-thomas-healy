/* The ABI marker follows the instrumentation, not the multilib.  A unit
   built for the ABI with the checks turned off calls nothing in the nullex
   runtime, so it must not assert a dependency on it -- otherwise every
   program that links libgcc's crtstuff, which is built this way, would
   demand the C++ runtime.  */
/* { dg-do compile { target { x86_64-*-linux* } } } */
/* { dg-options "-m64nullex -fno-nullptr-exceptions" } */

int
f (int *p)
{
  return *p;
}

/* { dg-final { scan-assembler-not "__nullex_abi" } } */
/* { dg-final { scan-assembler-not "__cxa_throw_null_pointer" } } */
