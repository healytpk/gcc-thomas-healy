/* __dualcall -- call-lowering support routine for the -m32df ABI.
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

/* Under -m32df a function pointer is 64 bits, packed as
   (entry:32 | (stack_top & ~1 | abi):32).  It reaches this helper in the
   EDX:EAX pair, which is simply how i386 holds a DImode value:

	%eax = entry	  -- the 32-bit code address of the callee
	%edx = stack|abi  -- a 16-byte-aligned address of the top of a stack to
			     run the call on, or 0 to use the caller's stack;
			     bit 0 is the calling-convention tag (16-aligned
			     means the low 4 bits are otherwise free)
	%ecx = argbytes	  -- size of the outgoing argument block to relocate

   The compiler emits, at each indirect call site:

	movl	<entry>, %eax
	movl	<stack|abi>, %edx
	movl	$<argbytes>, %ecx
	call	__dualcall

   with the arguments already pushed, exactly as for a normal call.

   The i386 difference.  On a register-argument target this helper only has to
   move the stack pointer, because the arguments ride in registers untouched.
   The i386 ABI passes arguments on the stack, so swapping %esp would strand
   them: the helper must copy the caller's outgoing argument block onto the
   stack it switches to.  The compiler passes that block's size in %ecx, so
   argument lists of any size are handled; the copy is read upwards from the
   caller's %esp and therefore always lies inside the caller's own frame.

   %ecx is scratch here, which means a callee declared with -mregparm (where
   %ecx would carry an argument) is not supported through a dual pointer.

   Scratch: EAX, ECX, EDX are call-clobbered and, apart from EAX, are not used
   to return values, so the callee's return value passes through untouched.
   EBX, ESI and EDI are callee-saved and are preserved across the copy.  */

#ifdef __M32DF__

	.text
	.globl	__dualcall
	.type	__dualcall, @function
	.p2align 4
__dualcall:
	.cfi_startproc
	andl	$-16, %edx		/* 16-align the top; clears the abi tag */
	jz	.Lplain			/* stack_top == 0 => caller's stack */

	/* Spill into the reserved area at the very top of the carried stack.
	   %ebx additionally carries the top across the call: it is callee-saved,
	   so the callee preserves it and the return path can find these slots
	   again without knowing how large the argument block was.  */
	movl	%ebx, -8(%edx)		/* caller's %ebx */
	movl	%esi, -12(%edx)
	movl	%edi, -16(%edx)
	movl	%eax, -20(%edx)		/* entry */
	movl	%esp, %eax		/* caller %esp: 4(%eax) = arguments */
	movl	%eax, -4(%edx)		/* remember it for the return path */

	/* Lay out the callee frame below the spill area, sized to the argument
	   block in %ecx, so that (%esp % 16) == 12 at the callee's entry --
	   what the i386 SysV ABI requires once CALL has pushed 4 bytes.  */
	addl	$3, %ecx
	andl	$-4, %ecx		/* whole dwords */
	leal	-32(%edx), %ebx
	subl	%ecx, %ebx
	andl	$-16, %ebx
	subl	$4, %ebx		/* %ebx = the new %esp */

	leal	4(%eax), %esi		/* copy the arguments across */
	leal	4(%ebx), %edi
	shrl	$2, %ecx
	rep movsl

	movl	-12(%edx), %esi		/* restore what the copy borrowed */
	movl	-16(%edx), %edi
	movl	-20(%edx), %ecx		/* %ecx = entry */
	movl	$.Lreturn, (%ebx)	/* the callee's return address */
	movl	%ebx, %esp		/* swap onto the carried stack */
	movl	%edx, %ebx		/* %ebx = top, preserved by the callee */
	jmp	*%ecx

.Lplain:
	/* No carried stack: jump straight to the callee.  __dualcall was
	   entered via CALL, so the callee's RET returns to the real caller.  */
	jmp	*%eax

.Lreturn:
	/* The callee returned with its result in EAX/EDX/ST(0), and %ebx still
	   pointing at the top of the carried stack.  Restore and return without
	   touching a value register.  */
	movl	-4(%ebx), %esp
	movl	-8(%ebx), %ebx
	ret
	.cfi_endproc
	.size	__dualcall, .-__dualcall

#endif /* __M32DF__ */

	.section	.note.GNU-stack,"",@progbits
