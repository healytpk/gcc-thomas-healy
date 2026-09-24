// Checked entry points for -fnullptr-exceptions.
// Copyright (C) 2026 Free Software Foundation, Inc.
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.
//
// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

// Under -fnullptr-exceptions a call to a C library function that declares
// a parameter nonnull is checked at the call site, in the caller's frame.
// That cannot work when the address of the function is taken instead of
// calling it: the pointer may be called from anywhere, including from code
// the compiler never sees.  So when the address of one of these functions
// is taken, the compiler hands back the address of the stub here instead,
// which performs the same check and then tail-calls the real function.
//
// There is one definition of each stub, in this library, so that two
// translation units taking the address of the same function still compare
// equal.  A per-translation-unit copy would break pointer identity inside
// the ABI, which would be worse than the gap being closed.
//
// The variadic entry points forward through the v-form of the same
// function rather than trying to re-pass an argument list, which is why
// none of this needs target-specific thunks.

#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cxxabi.h>

namespace
{
  // Mirrors the compiler's own check: throw only when the pointer is null
  // and every condition guarding the attribute holds.
  inline void
  check (const void* __p)
  {
    if (__builtin_expect (__p == 0, 0))
      __cxxabiv1::__cxa_throw_null_pointer_argument ();
  }

  // For the size-gated forms, where nonnull only applies when the length
  // is non-zero: memcpy (0, 0, 0) is well defined and must not throw.
  inline void
  check_if (const void* __p, __SIZE_TYPE__ __n)
  {
    if (__builtin_expect (__n != 0 && __p == 0, 0))
      __cxxabiv1::__cxa_throw_null_pointer_argument ();
  }
}

extern "C"
{

#ifdef __NULLEX_ABI__
// Defined only in the copy of this library built for the ABI.  Every object
// compiled for the ABI carries a reference to it, so linking those objects
// against the ordinary runtime fails here, rather than producing a program
// in which one function has two addresses that compare unequal.
char __nullex_abi_v1 = 1;
#endif

// --- <string.h>: the size-gated group ------------------------------------

void*
__nullex_memcpy (void* __d, const void* __s, __SIZE_TYPE__ __n)
{ check_if (__d, __n); check_if (__s, __n); return std::memcpy (__d, __s, __n); }

void*
__nullex_memmove (void* __d, const void* __s, __SIZE_TYPE__ __n)
{ check_if (__d, __n); check_if (__s, __n); return std::memmove (__d, __s, __n); }

void*
__nullex_memset (void* __s, int __c, __SIZE_TYPE__ __n)
{ check_if (__s, __n); return std::memset (__s, __c, __n); }

int
__nullex_memcmp (const void* __a, const void* __b, __SIZE_TYPE__ __n)
{ check_if (__a, __n); check_if (__b, __n); return std::memcmp (__a, __b, __n); }

// The return types below follow the C declarations, which is what the
// compiler hands us the type of when the address is taken in C.  The C++
// headers declare const-preserving overloads of the same functions; the
// representation is identical and the linkage is C, so the cast is only
// to keep this file honest about which declaration it is standing in for.
void*
__nullex_memchr (const void* __s, int __c, __SIZE_TYPE__ __n)
{
  check_if (__s, __n);
  return const_cast<void*> (std::memchr (__s, __c, __n));
}

// --- <string.h>: the unconditional group ---------------------------------

char*
__nullex_strcpy (char* __d, const char* __s)
{ check (__d); check (__s); return std::strcpy (__d, __s); }

char*
__nullex_strncpy (char* __d, const char* __s, __SIZE_TYPE__ __n)
{ check (__d); check (__s); return std::strncpy (__d, __s, __n); }

char*
__nullex_strcat (char* __d, const char* __s)
{ check (__d); check (__s); return std::strcat (__d, __s); }

char*
__nullex_strncat (char* __d, const char* __s, __SIZE_TYPE__ __n)
{ check (__d); check (__s); return std::strncat (__d, __s, __n); }

int
__nullex_strcmp (const char* __a, const char* __b)
{ check (__a); check (__b); return std::strcmp (__a, __b); }

int
__nullex_strncmp (const char* __a, const char* __b, __SIZE_TYPE__ __n)
{ check (__a); check (__b); return std::strncmp (__a, __b, __n); }

__SIZE_TYPE__
__nullex_strlen (const char* __s)
{ check (__s); return std::strlen (__s); }

char*
__nullex_strchr (const char* __s, int __c)
{ check (__s); return const_cast<char*> (std::strchr (__s, __c)); }

char*
__nullex_strrchr (const char* __s, int __c)
{ check (__s); return const_cast<char*> (std::strrchr (__s, __c)); }

char*
__nullex_strstr (const char* __h, const char* __n)
{ check (__h); check (__n); return const_cast<char*> (std::strstr (__h, __n)); }

__SIZE_TYPE__
__nullex_strspn (const char* __s, const char* __a)
{ check (__s); check (__a); return std::strspn (__s, __a); }

__SIZE_TYPE__
__nullex_strcspn (const char* __s, const char* __r)
{ check (__s); check (__r); return std::strcspn (__s, __r); }

char*
__nullex_strpbrk (const char* __s, const char* __a)
{ check (__s); check (__a); return const_cast<char*> (std::strpbrk (__s, __a)); }

// --- <stdio.h>: the variadic group ---------------------------------------
//
// Each forwards through the v-form, so the argument list is passed on
// without the stub having to reconstruct it.

int
__nullex_printf (const char* __fmt, ...)
{
  check (__fmt);
  va_list __ap;
  va_start (__ap, __fmt);
  int __r = std::vprintf (__fmt, __ap);
  va_end (__ap);
  return __r;
}

int
__nullex_fprintf (std::FILE* __f, const char* __fmt, ...)
{
  check (__f);
  check (__fmt);
  va_list __ap;
  va_start (__ap, __fmt);
  int __r = std::vfprintf (__f, __fmt, __ap);
  va_end (__ap);
  return __r;
}

int
__nullex_sprintf (char* __b, const char* __fmt, ...)
{
  check (__b);
  check (__fmt);
  va_list __ap;
  va_start (__ap, __fmt);
  int __r = std::vsprintf (__b, __fmt, __ap);
  va_end (__ap);
  return __r;
}

int
__nullex_snprintf (char* __b, __SIZE_TYPE__ __n, const char* __fmt, ...)
{
  check_if (__b, __n);
  check (__fmt);
  va_list __ap;
  va_start (__ap, __fmt);
  int __r = std::vsnprintf (__b, __n, __fmt, __ap);
  va_end (__ap);
  return __r;
}

int
__nullex_sscanf (const char* __s, const char* __fmt, ...)
{
  check (__s);
  check (__fmt);
  va_list __ap;
  va_start (__ap, __fmt);
  int __r = std::vsscanf (__s, __fmt, __ap);
  va_end (__ap);
  return __r;
}

} // extern "C"
