# `-m32df`: dual function pointers on an i386 base

`-m32df` is an x86 ABI in which **data pointers are 4 bytes but function
pointers are 8**. It exists as an implementation proof for the proposal to add
`intfptr_t` / `uintfptr_t` to `<cstdint>`: the standard defines `[u]intptr_t`
for *object* pointers only, so on a target like this one no standard integer
type is guaranteed to round-trip a function pointer.

| type | size |
|------|------|
| `void *`, `int *` | **4** |
| `long`, `size_t` | **4** |
| function pointer | **8** |
| `uintptr_t` | 4 — too narrow to hold a function pointer |
| `uintfptr_t` / `intfptr_t` | **8** |

The base is i386, so a `-m32df` program is an ordinary `ELF 32-bit, Intel
80386` executable. It runs on any x86-64 Linux, because ia32 emulation is
essentially always enabled — unlike the x32 ABI, whose process support is
compiled out or disabled by default on current kernels.

## The dual pointer

A function pointer is 64 bits, and the upper half is not padding:

```
 63                      32 31                    1   0
+--------------------------+----------------------+---+
|   stack_top (upper bits) | stack_top (lower)    |abi|
+--------------------------+----------------------+---+
|                     entry (32)                       |
+------------------------------------------------------+
```

* **entry** — the 32-bit code address of the callee.
* **stack_top** — a 16-byte-aligned address of the top of a heap-allocated
  stack the call should run on, or 0 to use the caller's stack. (16-aligned
  means the low four bits are free, so bit 0 can carry the tag.)
* **abi** — 0 = System V, 1 = Microsoft. The *caller* places arguments per the
  chosen convention; the runtime helper performs only the call mechanism.

A plain function address such as `&f` has a zero upper half — "here, now,
default convention" — so ordinary function pointers behave exactly as usual.

## How a dual call is lowered

Under `-m32df` every indirect call goes through the `__dualcall` helper in
libgcc. The compiler emits, at the call site:

```
	movl	<entry>, %eax
	movl	<stack|abi>, %edx	# the dual pointer, as a DImode pair
	movl	$<argbytes>, %ecx
	call	__dualcall
```

`EDX:EAX` is simply how i386 holds a 64-bit value, so the dual pointer arrives
split for free. Direct calls are untouched, and a dual pointer is never
tail-called — the helper has to regain control to restore the stack.

The i386 difference worth knowing: **arguments are passed on the stack**, so
unlike a register-argument target the helper cannot just move `%esp` — that
would strand them. The compiler therefore also passes the outgoing argument
block size in `%ecx`, and `__dualcall` relocates that block onto the stack it
swaps in. Argument lists of any size work.

## Which C library

`m32`, `m64` and `mx32` use glibc exactly as before. `m32df` cannot: widening
function pointers changes the size of every structure holding one, and each
indirect call across the library boundary would disagree about the pointer's
width. No newlib port for this ABI exists either, so `m32df` links against the
small C library in `libgcc/config/i386/m32df-libc.c` — `printf`, `malloc`, the
`mem*`/`str*` family, the syscall layer, and the C++ runtime support routines
(`new`/`delete`, guard variables, `__cxa_atexit`, static construction).

It lives in libgcc rather than a separate target library for a structural
reason: libgcc is built for every multilib, and the file guards itself with
`#ifdef __M32DF__`, so it compiles to **nothing** in the other multilibs — 42
global symbols under `-m32df`, zero elsewhere. No library on the search path
can shadow glibc. `LIB_SPEC` for `m32df` is correspondingly empty; the driver
links libgcc regardless.

This also sidesteps a limitation of GCC's build system: a target library is
always built for the *default* multilib as well as each configured one, so a
real newlib could not have been confined to `m32df` alone — its `libc.a` would
have installed where the driver finds it ahead of glibc and hijacked `-m64`.

## Building programs

```sh
gcc -m32df demo.c -o demo
./demo
```

```
-m32df
  sizeof(void*)      = 4
  sizeof(fn pointer) = 8
  sizeof(uintptr_t)  = 4  (cannot hold a function pointer)
  sizeof(uintfptr_t) = 8  (can)
  libc: "malloc works" (12 chars) at 0x...
  plain call: ran on the current stack  OK
  dual  call: ran on the carried stack  OK
PASS
```

The driver forces `-static` (there is no dynamic C library for this ABI),
`-nolibc` (which suppresses the `-latomic_asneeded` the generic GNU/Linux link
sequence would add; libatomic is not built for `m32df`, as it needs a hosted
libc and pthreads) and `-no-pie`, since `__dualcall` plants an absolute address
for its return trampoline.

### C++

Run `contrib/m32df/install-c++config.sh <prefix>` once after `make install`.
libstdc++ is not built for this multilib, so its configure never generates the
per-multilib `<bits/c++config.h>` that every libstdc++ header includes; the
script supplies it from the `m32` multilib, which is correct because both are
i386/ILP32. With that in place the header-only freestanding subset --
`<cstdint>`, `<type_traits>`, `<limits>`, `<new>`, `<initializer_list>` --
works and `-nostdinc++` is no longer needed.

**Exceptions and RTTI are not supported**, so `-fno-exceptions -fno-rtti` are
required. This is the one respect in which the implementation is not a
conforming freestanding one: freestanding subsets the *library*, not the
language. Closing the gap needs libsupc++ built for this multilib -- in
particular `__gxx_personality_v0`, which parses the LSDA and matches handlers
through `std::type_info` -- and libsupc++ is part of libstdc++. The unwinder
itself is not the obstacle: `_Unwind_Ptr` is address-sized here, so the
64-bit function pointers leave the EH tables untouched.

`g++` works directly, with no special flags:

```sh
g++ -m32df tester.cpp -o prog
```

`-fno-exceptions` and `-fno-rtti` are supplied automatically for this ABI (via
`CC1PLUS_SPEC`, so they reach only C++ compilations -- `-fno-rtti` is not a C
option). Passing `-fexceptions` or `-frtti` explicitly overrides that and
fails at link, which is the honest outcome: see below.

libstdc++ is not built for this multilib, and the `g++` driver appends
`-lstdc++` as a command-line argument rather than through a spec, so no spec
can remove it. A stub `libstdc++.a` is therefore installed into this
multilib's directory only (see `libgcc/config/i386/t-m32df`), which satisfies
the link. Nothing is lost by its being empty: the C++ runtime support a
freestanding program actually calls -- `operator new` and `delete`,
`__cxa_pure_virtual`, guard variables, `__cxa_atexit` and static construction
-- lives in libgcc, which is linked after it.

`-fno-exceptions -fno-rtti` are required: throwing needs the unwinder and
type-info machinery that live in libstdc++/libsupc++. Supported: `new` and
`delete`, virtual functions, function-local statics, objects of static storage
duration with constructors and destructors, `atexit`. Templates, classes and
`constexpr` are front-end features and always worked. Not available: the
standard *library* — use the C entry points declared `extern "C"`.

Static construction exposes the ABI split nicely. GCC emits `.init_array`
entries as `.long` — *address*-sized, because an address is 32 bits here —
while a function pointer is 64 bits. The runtime reads those arrays as 32-bit
words and widens each before calling; treating them as an array of function
pointers would stride by 8 and read garbage. With no hosted libc to run them,
`crt0-m32df.S` calls `__m32df_run_init_array` before `main`, and `exit` runs
the destructors registered by `__cxa_atexit` followed by `.fini_array`.

## Files

| path | purpose |
|---|---|
| `libgcc/config/i386/dualcall.S` | the `__dualcall` call-lowering helper |
| `libgcc/config/i386/crt0-m32df.S` | freestanding startup code (`crt0.o`) |
| `libgcc/config/i386/m32df-libc.c` | the minimal C library |
| `libgcc/config/i386/t-m32df` | builds all three for the `m32df` multilib |
| `contrib/m32df/demo.c` | the demo above |
| `gcc/testsuite/gcc.target/i386/m32df-{abi,dualcall}.c` | compile tests |

## Limitations

* A callee reached through a dual pointer may not use `-mregparm`: `%ecx` is
  scratch in the call sequence.
* The ABI tag selects the caller's argument placement only; Microsoft-x64
  shadow space is not reserved, so use the carried-stack path with System V
  callees.
* The callee must respect the size of the stack the pointer carries.

## Printing with `std::cout`

libstdc++ is not built for this multilib, so the real `std::cout` is
unavailable: the object lives in `libstdc++.a`, and constructing it runs
`std::ios_base::Init`, which drags in the locale machinery -- roughly 20,000
lines needing a hosted C library. `contrib/m32df/include` supplies a small
substitute so freestanding programs can print:

```sh
g++ -m32df prog.cpp -o prog
```

No `-I` is needed: `install-c++config.sh` puts these headers in the compiler's
own tree and `CC1PLUS_SPEC` adds that directory for `-m32df` C++ compilations,
via `-iwithprefixbefore`, which resolves against the `-iprefix` the driver
already passes. It has to be searched *ahead* of the standard C++ directories
to take precedence over the real `<iostream>`, because the generic C++
directory is searched before the per-multilib one (see `gcc/cppdefault.cc`).
A directory that was never installed is silently ignored.

```cpp
#include <iostream>
#include <iomanip>

int main()
{
  unsigned v = 0xdeadbeef;
  std::cout << "0x" << std::hex << v << std::endl;
  std::cout << "padded: [" << std::setw(8) << std::setfill('0') << 255 << "]\n";
}
```

Provided: `<iostream>`, `<ostream>`, `<ios>`, `<iomanip>`; `std::cout`,
`std::cerr`, `std::clog`; insertion of C strings, `char`, `bool`, every
signed and unsigned integer type, and `const void *`; the manipulators `dec`,
`hex`, `oct`, `showbase`, `uppercase`, `endl`, `ends`, `flush`, and `setw`,
`setfill`, `setbase`.

Function pointers print as their full 64-bit value, zero-padded to sixteen
digits so the two halves line up:

```
plain fn ptr  : 0x0000000008049f40
carrying stack: 0x0804000008049f40
```

A real `std::ostream` has no overload for a function pointer, so `cout << f`
converts to `bool` and prints `1` -- a well known trap. Here it would not even
do that: a function pointer is eight bytes and does not convert to
`const void *` at all. The overload prints the value instead, which on this
ABI also shows whether the pointer carries a stack. Pointers to *member*
functions are not covered and still decay to `bool`.

**This is not a conforming `<ostream>`** and does not pretend to be. There is
no `streambuf`, no locale, no floating point, no wide characters, no input,
no exceptions, and no formatting state beyond base, width and fill. Output is
unbuffered -- every insertion is a `write(2)`. `setw` applies to the next
insertion only, as in a real stream. A function pointer is 8 bytes here and
does not convert to `const void *`, so cast it to `uintfptr_t` to print it.

It exists so that `-m32df` demonstration programs can produce output; use
`printf` from the freestanding C library if you would rather not depend on it.

## Passing a function pointer to the kernel

Yes -- truncate. The kernel is a stock i386 kernel that knows nothing of this
ABI, so a `sa_handler` slot in `struct sigaction`, or the `fn` argument to
`clone`, is four bytes wide. The low 32 bits of a dual pointer *are* the code
address, so narrowing produces exactly what the kernel expects, and it will
call it as an ordinary function.

Two rules follow.

**Declare kernel-facing fields as address-sized, not as function pointers.**
A structure shared with the kernel -- or with any library built for a
different ABI -- must use `uintptr_t` for a callback slot. Declaring it
`void (*)(int)` would make the field eight bytes and silently misalign every
member after it:

```c
struct kernel_sigaction { uintptr_t sa_handler; unsigned long sa_mask; };
...
act.sa_handler = (uintptr_t) handler;                  /* narrowing, deliberate */
void (*back)(int) = (void (*)(int)) act.sa_handler;    /* widening, high half 0 */
```

The conversions need no helper: narrowing a `__dualcode` pointer to an
integer truncates and widening zero-extends, so a pointer that made the round
trip is still perfectly callable. The compiler also warns at the boundary --
`-Wpointer-to-int-cast`, "cast from pointer to integer of different size" --
which is worth leaving on, because it marks precisely the places where the
upper half is dropped.

**A pointer that crosses that boundary loses any stack it carried.** The
upper half is the carried stack and the ABI tag, and there is nowhere in the
kernel's four-byte slot to put them. A signal handler registered this way runs
on the stack the kernel provides, like any other. Round-tripping a plain
function pointer is lossless, because its upper half is already zero; round-
tripping one that carries a stack yields a plain pointer:

```
dual ptr high half before=0x08040000 after=0x00000000
```

This is a good illustration of why both types are needed: `uintptr_t` is the
right type for the wire format, and `uintfptr_t` is the right type for the
language-level pointer. Neither can do the other's job.
