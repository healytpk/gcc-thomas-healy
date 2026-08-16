/* Freestanding startup code for the -m32df ABI.
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

/* Entry point for a static -m32df program linked without a C library.  The
   i386 address space is 32 bits throughout, so unlike a 64-bit host there is
   no stack to relocate: the kernel's stack is already reachable from a 32-bit
   pointer, and argc/argv are passed straight through to main.  */

#ifdef __M32DF__

#define SYS_EXIT	1			/* __NR_exit */

	.text
	.globl _start
	.type _start,@function
_start:
	xorl	%ebp, %ebp		/* outermost frame */
	movl	(%esp), %ebx		/* argc */
	leal	4(%esp), %esi		/* argv */
	andl	$-16, %esp		/* 16-align before any call */

	/* No hosted C library runs .init_array for us, so do it here: this is
	   what constructs objects of static storage duration and runs any
	   __attribute__((constructor)) functions.  */
	call	__m32df_run_init_array

	/* main (argc, argv).  The two pushes plus the CALL leave (esp%16)==12
	   at main's entry, which is what the i386 SysV ABI requires.  */
	subl	$8, %esp
	pushl	%esi
	pushl	%ebx
	call	main
	addl	$16, %esp

	/* exit() runs the destructors registered by __cxa_atexit and atexit,
	   then .fini_array, before leaving.  */
	subl	$12, %esp
	pushl	%eax
	call	exit
	hlt
	.size _start, .-_start

#endif /* __M32DF__ */

	.section	.note.GNU-stack,"",@progbits
