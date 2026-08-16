/* Minimal C library for the -m32df ABI.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* The -m32df ABI widens function pointers to 64 bits, so it is ABI-
   incompatible with any stock C library: a struct holding a function pointer
   changes size, and every indirect call across the library boundary would
   disagree about the pointer's width.  glibc therefore cannot be used, and no
   newlib port for this ABI exists.

   This file supplies the small freestanding C library that -m32df programs
   link against instead.  It lives in libgcc rather than in a separate target
   library for a structural reason: libgcc is built for every multilib, and the
   __M32DF__ guard below makes this file compile to nothing in the m32, m64 and
   mx32 multilibs.  Those ABIs therefore keep using glibc exactly as before,
   and there is no library on the search path that could shadow it.

   Everything here is self-contained: no headers are included, because there is
   no C library to include them from.

   Syscalls use the ordinary 64-bit numbers rather than the x32-specific ones
   (__X32_SYSCALL_BIT | nr).  An x32 process may invoke the 64-bit syscall
   table directly, and the calls used here pass only scalars and raw pointers,
   so no structure-layout difference arises.  The same object therefore works
   both in a true x32 process and in an ELF64 container on a kernel without x32
   support.  */

#ifdef __M32DF__

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int size_t;
typedef int ssize_t;
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start (ap, last)
#define va_arg(ap, type) __builtin_va_arg (ap, type)
#define va_end(ap) __builtin_va_end (ap)
#define NULL ((void *) 0)

/* ------------------------------------------------------------------ */
/* Raw system calls (i386: int $0x80, arguments in EBX/ECX/EDX).        */
/* ------------------------------------------------------------------ */

#define SYS_exit	1
#define SYS_read	3
#define SYS_write	4
#define SYS_brk		45

static inline int
syscall1 (int n, int a)
{
  int r;
  __asm__ volatile ("int $0x80" : "=a" (r) : "a" (n), "b" (a) : "memory");
  return r;
}

static inline int
syscall3 (int n, int a, int b, int c)
{
  int r;
  __asm__ volatile ("int $0x80"
		    : "=a" (r) : "a" (n), "b" (a), "c" (b), "d" (c)
		    : "memory");
  return r;
}

/* A data pointer is already 32 bits here, so no widening is needed.  */
#define PTRARG(p) ((int) (unsigned) (p))

void
_exit (int status)
{
  syscall1 (SYS_exit, status);
  __builtin_unreachable ();
}

ssize_t
write (int fd, const void *buf, size_t n)
{
  return (ssize_t) syscall3 (SYS_write, fd, PTRARG (buf), (int) n);
}

ssize_t
read (int fd, void *buf, size_t n)
{
  return (ssize_t) syscall3 (SYS_read, fd, PTRARG (buf), (int) n);
}

void
abort (void)
{
  _exit (134);
}

/* ------------------------------------------------------------------ */
/* Memory and string primitives.                                        */
/*                                                                      */
/* These carry no-tree-loop-distribute-patterns so the compiler does not */
/* recognise the copy/fill loops and rewrite them into calls to the very */
/* functions being defined here.                                         */
/* ------------------------------------------------------------------ */

#define NO_SELF_CALL __attribute__((__optimize__ ("no-tree-loop-distribute-patterns")))

NO_SELF_CALL void *
memcpy (void *dst, const void *src, size_t n)
{
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}

NO_SELF_CALL void *
memset (void *dst, int c, size_t n)
{
  unsigned char *d = dst;
  while (n--)
    *d++ = (unsigned char) c;
  return dst;
}

void *
memmove (void *dst, const void *src, size_t n)
{
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d == s || n == 0)
    return dst;
  if (d < s)
    while (n--)
      *d++ = *s++;
  else
    {
      d += n;
      s += n;
      while (n--)
	*--d = *--s;
    }
  return dst;
}

int
memcmp (const void *a, const void *b, size_t n)
{
  const unsigned char *p = a, *q = b;
  while (n--)
    {
      if (*p != *q)
	return (int) *p - (int) *q;
      p++, q++;
    }
  return 0;
}

size_t
strlen (const char *s)
{
  const char *p = s;
  while (*p)
    p++;
  return (size_t) (p - s);
}

int
strcmp (const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

int
strncmp (const char *a, const char *b, size_t n)
{
  while (n && *a && *a == *b)
    a++, b++, n--;
  if (n == 0)
    return 0;
  return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

char *
strcpy (char *d, const char *s)
{
  char *r = d;
  while ((*d++ = *s++))
    ;
  return r;
}

char *
strncpy (char *d, const char *s, size_t n)
{
  char *r = d;
  while (n && *s)
    *d++ = *s++, n--;
  while (n--)
    *d++ = '\0';
  return r;
}

char *
strcat (char *d, const char *s)
{
  strcpy (d + strlen (d), s);
  return d;
}

char *
strchr (const char *s, int c)
{
  for (;; s++)
    {
      if (*s == (char) c)
	return (char *) s;
      if (!*s)
	return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Heap: sbrk plus a first-fit allocator.                               */
/* ------------------------------------------------------------------ */

static char *cur_brk;

void *
sbrk (int increment)
{
  char *old, *want;

  if (cur_brk == NULL)
    cur_brk = (char *) (unsigned) syscall1 (SYS_brk, 0);

  old = cur_brk;
  want = old + increment;
  if ((unsigned) syscall1 (SYS_brk, PTRARG (want)) < (unsigned) want)
    return (void *) -1;
  cur_brk = want;
  return old;
}

struct block
{
  size_t size;			/* payload bytes */
  struct block *next;
  int free;
};

#define BLOCK_HDR ((size_t) sizeof (struct block))
#define ALIGN_UP(n) (((n) + 15u) & ~15u)

static struct block *heap_head;

void
free (void *p)
{
  struct block *b, *n;

  if (p == NULL)
    return;
  b = (struct block *) ((char *) p - BLOCK_HDR);
  b->free = 1;

  /* Coalesce forward with any adjacent free blocks.  */
  for (n = b->next; n && n->free; n = b->next)
    {
      b->size += BLOCK_HDR + n->size;
      b->next = n->next;
    }
}

void *
malloc (size_t n)
{
  struct block *b, *last = NULL, *nb;
  char *mem;

  if (n == 0)
    return NULL;
  n = ALIGN_UP (n);

  for (b = heap_head; b; last = b, b = b->next)
    if (b->free && b->size >= n)
      {
	/* Split when the remainder can hold a header plus a little.  */
	if (b->size >= n + BLOCK_HDR + 16)
	  {
	    nb = (struct block *) ((char *) b + BLOCK_HDR + n);
	    nb->size = b->size - n - BLOCK_HDR;
	    nb->next = b->next;
	    nb->free = 1;
	    b->size = n;
	    b->next = nb;
	  }
	b->free = 0;
	return (char *) b + BLOCK_HDR;
      }

  mem = sbrk ((int) (BLOCK_HDR + n));
  if (mem == (char *) -1)
    return NULL;
  b = (struct block *) mem;
  b->size = n;
  b->next = NULL;
  b->free = 0;
  if (last)
    last->next = b;
  else
    heap_head = b;
  return mem + BLOCK_HDR;
}

void *
calloc (size_t count, size_t size)
{
  size_t total = count * size;
  void *p = malloc (total);
  if (p)
    memset (p, 0, total);
  return p;
}

void *
realloc (void *p, size_t n)
{
  struct block *b;
  void *q;

  if (p == NULL)
    return malloc (n);
  b = (struct block *) ((char *) p - BLOCK_HDR);
  if (b->size >= n)
    return p;
  q = malloc (n);
  if (q)
    {
      memcpy (q, p, b->size);
      free (p);
    }
  return q;
}

/* ------------------------------------------------------------------ */
/* Formatted output.                                                    */
/* ------------------------------------------------------------------ */

struct sink
{
  char *buf;			/* NULL => write to fd */
  size_t cap;			/* capacity when buffering */
  size_t len;			/* characters emitted (excluding NUL) */
  int fd;
  char stage[256];
  unsigned stage_n;
};

static void
sink_flush (struct sink *s)
{
  if (s->buf == NULL && s->stage_n)
    write (s->fd, s->stage, s->stage_n);
  s->stage_n = 0;
}

static void
sink_put (struct sink *s, char c)
{
  if (s->buf)
    {
      if (s->len + 1 < s->cap)
	s->buf[s->len] = c;
    }
  else
    {
      s->stage[s->stage_n++] = c;
      if (s->stage_n == sizeof s->stage)
	sink_flush (s);
    }
  s->len++;
}

static void
sink_pad (struct sink *s, char c, int n)
{
  while (n-- > 0)
    sink_put (s, c);
}

/* Divide *VALP by BASE, returning the remainder.  Written as a shift-and-
   subtract long division so that this file needs no 64-bit division helper
   from libgcc: every shift here is by a constant, which the compiler expands
   inline on a 32-bit target.  */

static unsigned
udivmod (u64 *valp, unsigned base)
{
  u64 v = *valp, q = 0;
  unsigned rem = 0;
  int i;

  for (i = 0; i < 64; i++)
    {
      unsigned bit = (unsigned) (v >> 63);
      v <<= 1;
      q <<= 1;
      rem = (rem << 1) | bit;
      if (rem >= base)
	{
	  rem -= base;
	  q |= 1;
	}
    }
  *valp = q;
  return rem;
}

/* Render VAL in BASE into BUF (written backwards); return its length.  */
static int
render (u64 val, unsigned base, int upper, char *buf)
{
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  int n = 0;

  if (val == 0)
    buf[n++] = '0';
  while (val)
    buf[n++] = digits[udivmod (&val, base)];
  return n;
}

static void
emit_number (struct sink *s, u64 val, unsigned base, int upper, int negative,
	     int width, int zero_pad, int left)
{
  char tmp[32];
  int n = render (val, base, upper, tmp);
  int total = n + (negative ? 1 : 0);

  if (!left && !zero_pad)
    sink_pad (s, ' ', width - total);
  if (negative)
    sink_put (s, '-');
  if (!left && zero_pad)
    sink_pad (s, '0', width - total);
  while (n--)
    sink_put (s, tmp[n]);
  if (left)
    sink_pad (s, ' ', width - total);
}

static int
format (struct sink *s, const char *fmt, va_list ap)
{
  for (; *fmt; fmt++)
    {
      int left = 0, zero_pad = 0, width = 0, longs = 0;
      u64 uval;
      s64 sval;
      const char *str;

      if (*fmt != '%')
	{
	  sink_put (s, *fmt);
	  continue;
	}

      fmt++;
      for (;; fmt++)
	{
	  if (*fmt == '-')
	    left = 1;
	  else if (*fmt == '0')
	    zero_pad = 1;
	  else
	    break;
	}
      while (*fmt >= '0' && *fmt <= '9')
	width = width * 10 + (*fmt++ - '0');
      for (; *fmt == 'l' || *fmt == 'z' || *fmt == 'h'; fmt++)
	if (*fmt == 'l')
	  longs++;

      switch (*fmt)
	{
	case 'd':
	case 'i':
	  /* long is 32-bit in this ILP32 ABI; only ll is 64-bit.  */
	  sval = longs >= 2 ? va_arg (ap, long long) : (s64) va_arg (ap, int);
	  if (sval < 0)
	    emit_number (s, (u64) -sval, 10, 0, 1, width, zero_pad, left);
	  else
	    emit_number (s, (u64) sval, 10, 0, 0, width, zero_pad, left);
	  break;

	case 'u':
	case 'x':
	case 'X':
	case 'o':
	  uval = (longs >= 2 ? va_arg (ap, unsigned long long)
		  : (u64) va_arg (ap, unsigned int));
	  emit_number (s, uval, *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16,
		       *fmt == 'X', 0, width, zero_pad, left);
	  break;

	case 'p':
	  sink_put (s, '0');
	  sink_put (s, 'x');
	  emit_number (s, (u64) (unsigned) va_arg (ap, void *), 16, 0, 0,
		       width, zero_pad, left);
	  break;

	case 'c':
	  if (!left)
	    sink_pad (s, ' ', width - 1);
	  sink_put (s, (char) va_arg (ap, int));
	  if (left)
	    sink_pad (s, ' ', width - 1);
	  break;

	case 's':
	  str = va_arg (ap, const char *);
	  if (str == NULL)
	    str = "(null)";
	  {
	    int n = (int) strlen (str);
	    if (!left)
	      sink_pad (s, ' ', width - n);
	    while (*str)
	      sink_put (s, *str++);
	    if (left)
	      sink_pad (s, ' ', width - n);
	  }
	  break;

	case '%':
	  sink_put (s, '%');
	  break;

	case '\0':
	  fmt--;
	  break;

	default:
	  sink_put (s, '%');
	  sink_put (s, *fmt);
	  break;
	}
    }

  if (s->buf)
    {
      if (s->cap)
	s->buf[s->len < s->cap ? s->len : s->cap - 1] = '\0';
    }
  else
    sink_flush (s);

  return (int) s->len;
}

int
vfprintf_fd (int fd, const char *fmt, va_list ap)
{
  struct sink s;
  memset (&s, 0, sizeof s);
  s.fd = fd;
  return format (&s, fmt, ap);
}

int
printf (const char *fmt, ...)
{
  va_list ap;
  int n;
  va_start (ap, fmt);
  n = vfprintf_fd (1, fmt, ap);
  va_end (ap);
  return n;
}

int
vsnprintf (char *buf, size_t cap, const char *fmt, va_list ap)
{
  struct sink s;
  memset (&s, 0, sizeof s);
  s.buf = buf;
  s.cap = cap;
  return format (&s, fmt, ap);
}

int
snprintf (char *buf, size_t cap, const char *fmt, ...)
{
  va_list ap;
  int n;
  va_start (ap, fmt);
  n = vsnprintf (buf, cap, fmt, ap);
  va_end (ap);
  return n;
}

int
puts (const char *s)
{
  write (1, s, strlen (s));
  write (1, "\n", 1);
  return 0;
}

int
putchar (int c)
{
  char ch = (char) c;
  write (1, &ch, 1);
  return c;
}

/* ------------------------------------------------------------------ */
/* Static constructors/destructors and C++ runtime support.             */
/*                                                                      */
/* There is no hosted C library to run .init_array for us, so the        */
/* startup code calls __m32df_run_init_array below.  The C++ entry       */
/* points are spelled with their Itanium-ABI mangled names so that this  */
/* C file can supply them; size_t is `unsigned int' in this ILP32 ABI,   */
/* hence the `j' in the mangled operator new/delete names.               */
/* ------------------------------------------------------------------ */

/* .init_array and .fini_array hold function pointers, which are 8 bytes in
   this ABI: the low word is the code address and the high word is zero, since
   ELF32 has no 64-bit relocation (see ix86_assemble_integer).  Walk them as
   pairs of 32-bit words and call the low one; a zero high half means "here,
   now, default convention", i.e. an ordinary call.  */

extern unsigned __init_array_start[] __attribute__((weak));
extern unsigned __init_array_end[] __attribute__((weak));
extern unsigned __fini_array_start[] __attribute__((weak));
extern unsigned __fini_array_end[] __attribute__((weak));

static inline void
call_address (unsigned addr)
{
  void (*fn) (void) = (void (*) (void)) (__UINTFPTR_TYPE__) addr;
  fn ();
}

void
__m32df_run_init_array (void)
{
  unsigned i, n = (unsigned) (__init_array_end - __init_array_start) / 2;
  for (i = 0; i < n; i++)
    call_address (__init_array_start[2 * i]);
}

static void
run_fini_array (void)
{
  unsigned n = (unsigned) (__fini_array_end - __fini_array_start) / 2;
  while (n--)
    call_address (__fini_array_start[2 * n]);
}

/* Destructors registered by atexit and __cxa_atexit.  */
struct atexit_entry
{
  void (*fn) (void *);
  void *arg;
  int has_arg;
};

#define MAX_ATEXIT 64
static struct atexit_entry atexit_fns[MAX_ATEXIT];
static unsigned atexit_n;

int
atexit (void (*fn) (void))
{
  if (atexit_n >= MAX_ATEXIT)
    return -1;
  atexit_fns[atexit_n].fn = (void (*) (void *)) fn;
  atexit_fns[atexit_n].has_arg = 0;
  atexit_n++;
  return 0;
}

/* Registration of a static object's destructor.  D is the DSO handle, which
   is meaningless for a static executable and ignored.  */
int
__cxa_atexit (void (*fn) (void *), void *arg, void *d __attribute__((unused)))
{
  if (atexit_n >= MAX_ATEXIT)
    return -1;
  atexit_fns[atexit_n].fn = fn;
  atexit_fns[atexit_n].arg = arg;
  atexit_fns[atexit_n].has_arg = 1;
  atexit_n++;
  return 0;
}

void
__cxa_finalize (void *d __attribute__((unused)))
{
}

/* crtbegin.o normally defines this; keep it weak so either may win.  */
void *__dso_handle __attribute__((weak)) = &__dso_handle;

/* Run destructors, then leave.  exit() is declared above and defined here so
   that it tears down before _exit.  */
void
exit (int status)
{
  while (atexit_n)
    {
      struct atexit_entry *e = &atexit_fns[--atexit_n];
      if (e->has_arg)
	e->fn (e->arg);
      else
	((void (*) (void)) e->fn) ();
    }
  run_fini_array ();
  _exit (status);
}

/* A call to a pure virtual function got through; there is no recovery.  */
void
__cxa_pure_virtual (void)
{
  write (2, "pure virtual call\n", 18);
  abort ();
}

void
__cxa_deleted_virtual (void)
{
  write (2, "deleted virtual call\n", 21);
  abort ();
}

/* Guard variables for function-local statics.  Single-threaded: the guard is
   a byte that is zero until the object has been constructed.  */
int
__cxa_guard_acquire (unsigned long long *g)
{
  return !*(char *) g;
}

void
__cxa_guard_release (unsigned long long *g)
{
  *(char *) g = 1;
}

void
__cxa_guard_abort (unsigned long long *g __attribute__((unused)))
{
}

/* operator new / operator delete.  Mangled by hand: size_t is unsigned int
   (`j') in this ILP32 ABI.  A failed allocation cannot throw std::bad_alloc
   without the unwinder, so it aborts.  */
void *_Znwj (size_t) __asm__ ("_Znwj");
void *_Znaj (size_t) __asm__ ("_Znaj");
void _ZdlPv (void *) __asm__ ("_ZdlPv");
void _ZdaPv (void *) __asm__ ("_ZdaPv");
void _ZdlPvj (void *, size_t) __asm__ ("_ZdlPvj");
void _ZdaPvj (void *, size_t) __asm__ ("_ZdaPvj");

void *
_Znwj (size_t n)
{
  void *p = malloc (n ? n : 1);
  if (p == NULL)
    {
      write (2, "operator new failed\n", 20);
      abort ();
    }
  return p;
}

void *
_Znaj (size_t n)
{
  return _Znwj (n);
}

void
_ZdlPv (void *p)
{
  free (p);
}

void
_ZdaPv (void *p)
{
  free (p);
}

void
_ZdlPvj (void *p, size_t n __attribute__((unused)))
{
  free (p);
}

void
_ZdaPvj (void *p, size_t n __attribute__((unused)))
{
  free (p);
}

#endif /* __M32DF__ */
