/* Verify that -m32df lowers an indirect call through the __dualcall helper,
   passing the whole 64-bit dual pointer in %r11, and leaves direct calls
   alone.  A dual call must not be a sibcall: __dualcall has to regain control
   to restore the stack it swapped in.  */
/* { dg-do compile { target { x86_64-*-* } } } */
/* { dg-options "-m32df -O2" } */

typedef int (*fp) (int, int);
extern int named (int, int);

int via_ptr (fp f, int a, int b) { return f (a, b); }
int via_named (int a, int b) { return named (a, b); }
int tail_ptr (fp f, int a, int b) { return f (a, b); }

/* { dg-final { scan-assembler "call\t__dualcall" } } */
/* { dg-final { scan-assembler "jmp\tnamed" } } */
/* { dg-final { scan-assembler-not "jmp\t__dualcall" } } */
