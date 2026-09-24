/* Objects built for the ABI carry a reference to a symbol that only that
   ABI's runtime defines, so linking them against the ordinary runtime
   fails at link time rather than producing a program in which one
   function has two addresses.  The section is retained, or --gc-sections
   would discard the reference and the check along with it.  */
/* { dg-do compile { target { x86_64-*-linux* } } } */
/* { dg-options "-m64nullex" } */

int
f (int *p)
{
  return *p;
}

/* { dg-final { scan-assembler "__nullex_abi_v1" } } */
/* { dg-final { scan-assembler "\\.rodata\\.__nullex_abi_ref,\"aGR\"" } } */
/* { dg-final { scan-assembler-not "__nullex_abi_ref\[^\n\]*\"aG\"" } } */
