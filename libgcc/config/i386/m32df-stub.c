/* Contents of the stub libstdc++.a for the -m32df multilib.
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

/* libstdc++ is not built for the -m32df multilib: it needs a hosted C
   library, and this ABI has only the small freestanding one in libgcc.  The
   g++ driver nevertheless appends -lstdc++ as a command-line argument rather
   than through a spec, so no spec can remove it and the link fails with
   "cannot find -lstdc++".

   A stub archive satisfies that reference.  Nothing is lost by its being
   empty: the C++ runtime support a freestanding program actually calls --
   operator new and delete, __cxa_pure_virtual, the guard variables for
   function-local statics, __cxa_atexit and static construction -- is in
   libgcc itself (see m32df-libc.c), and libgcc is linked after libstdc++, so
   those references resolve there.

   This object exists only so the archive is well formed; an archive with no
   members is legal but is treated inconsistently by some tools.  */

#ifdef __M32DF__
const char __m32df_libstdcxx_stub[]
  = "stub libstdc++ for -m32df; C++ runtime support is in libgcc";
#endif
