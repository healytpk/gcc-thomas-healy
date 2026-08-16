/* A running demonstration of the -m32df ABI, using the minimal C library
   built into this ABI's libgcc (libgcc/config/i386/m32df-libc.c).

   It shows that:
     - data pointers are 4 bytes while function pointers are 8, so uintptr_t
       cannot round-trip a function pointer and uintfptr_t is needed;
     - ordinary C works: printf, malloc, the string functions;
     - a call through a dual function pointer whose high 32 bits carry a stack
       address really does execute on that stack and return.  */

int printf (const char *, ...);
void *malloc (unsigned);
void free (void *);
char *strcpy (char *, const char *);
unsigned strlen (const char *);

typedef void (*fp0) (void);

_Static_assert (sizeof (void *) == 4, "data pointer is 32-bit");
_Static_assert (sizeof (fp0) == 8, "function pointer is 64-bit");
_Static_assert (sizeof (__UINTPTR_TYPE__) == 4, "uintptr_t is too narrow");
_Static_assert (sizeof (__UINTFPTR_TYPE__) == 8, "uintfptr_t is wide enough");
_Static_assert (sizeof (void *) < sizeof (fp0),
		"the point: a function pointer does not fit in a void *");

static char stackbuf[16384] __attribute__((aligned (16)));
static volatile int on_custom;

static void
probe (void)
{
  register unsigned sp __asm__ ("esp");
  unsigned s = sp;
  on_custom = (s > (unsigned) stackbuf
	       && s <= (unsigned) stackbuf + sizeof stackbuf);
}

int
main (void)
{
  char *heap;
  fp0 plain, dual;
  __UINTFPTR_TYPE__ entry;
  unsigned top;
  int plain_ok, dual_ok;

  printf ("-m32df\n");
  printf ("  sizeof(void*)      = %u\n", (unsigned) sizeof (void *));
  printf ("  sizeof(fn pointer) = %u\n", (unsigned) sizeof (fp0));
  printf ("  sizeof(uintptr_t)  = %u  (cannot hold a function pointer)\n",
	  (unsigned) sizeof (__UINTPTR_TYPE__));
  printf ("  sizeof(uintfptr_t) = %u  (can)\n",
	  (unsigned) sizeof (__UINTFPTR_TYPE__));

  heap = malloc (32);
  strcpy (heap, "malloc works");
  printf ("  libc: \"%s\" (%u chars) at %p\n", heap, strlen (heap), heap);
  free (heap);

  /* A plain function pointer: high half zero => run on the current stack.  */
  on_custom = -1;
  plain = &probe;
  plain ();
  plain_ok = (on_custom == 0);

  /* A dual pointer carrying a stack in its high 32 bits.  The call is lowered
     to "movq <ptr>, %r11 ; call __dualcall", and the helper swaps stacks.  */
  entry = (__UINTFPTR_TYPE__) &probe & 0xFFFFFFFFULL;
  top = ((unsigned) stackbuf + sizeof stackbuf) & ~15u;
  dual = (fp0) (entry | ((__UINTFPTR_TYPE__) (top & ~1u) << 32));
  on_custom = -1;
  dual ();
  dual_ok = (on_custom == 1);

  printf ("  plain call: %s\n",
	  plain_ok ? "ran on the current stack  OK" : "FAILED");
  printf ("  dual  call: %s\n",
	  dual_ok ? "ran on the carried stack  OK" : "FAILED");
  printf ("%s\n", plain_ok && dual_ok ? "PASS" : "FAIL");
  return !(plain_ok && dual_ok);
}
