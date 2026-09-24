/* The same, reached the way libgcc's crtstuff reaches it: -fnullptr-exceptions
   is only implied by the multilib here, so an explicit -fno-exceptions turns
   it back off without a diagnostic, and no marker may be left behind.  */
/* { dg-do compile { target { x86_64-*-linux* } } } */
/* { dg-options "-m64nullex -fno-exceptions" } */

int
f (int *p)
{
  return *p;
}

/* { dg-final { scan-assembler-not "__nullex_abi" } } */
/* { dg-final { scan-assembler-not "__cxa_throw_null_pointer" } } */
