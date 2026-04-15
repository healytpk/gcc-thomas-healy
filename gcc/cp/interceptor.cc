/* Interceptor functions.
   Copyright (C) 1992-2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* Contents of this file:

Definition of types:
    - struct pending_interceptor_thunk

Definitions of global variables: 
    - pending_interceptor_thunks
    - tree current_interceptor_thunk_decl
    - char *current_interceptor__core_asm_name

Definitions of functions:
    extern - handle_interceptor_attribute
    static - configure_interceptor_core
    extern - start_function_interceptor
    static - emit_pure_assembly_as_function
    static - get_assembler_for_interceptor_thunk
    extern - queue_emission_of_interceptor_thunk_if_decl_is_interceptor
    extern - flush_pending_interceptor_thunks 
*/

#include "interceptor.h"
#define INCLUDE_STRING
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "c-family/c-target.h"
#include "cp-tree.h"
#include "timevar.h"
#include "stringpool.h"
#include "cgraph.h"
#include "stor-layout.h"
#include "varasm.h"
#include "attribs.h"
#include "flags.h"
#include "tree-iterator.h"
#include "decl.h"
#include "interceptor.h"
#include "intl.h"
#include "toplev.h"
#include "c-family/c-objc.h"
#include "c-family/c-pragma.h"
#include "c-family/c-ubsan.h"
#include "cp/cp-name-hint.h"
#include "debug.h"
#include "plugin.h"
#include "builtins.h"
#include "gimplify.h"
#include "asan.h"
#include "gcc-rich-location.h"
#include "langhooks.h"
#include "context.h"  /* For 'g'.  */
#include "omp-general.h"
#include "omp-offload.h"  /* For offload_vars.  */
#include "opts.h"
#include "langhooks-def.h"  /* For lhd_simulate_record_decl  */
#include "coroutines.h"
#include "contracts.h"
#include "gcc-urlifier.h"
#include "diagnostic-highlight-colors.h"
#include "pretty-print-markup.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "function.h"
#include "tree.h"
#include "memmodel.h"
#include "../c-family/c-common.h"
#include "gimple-expr.h"
#include "tm_p.h"
#include "stringpool.h"
#include "cgraph.h"
#include "diagnostic.h"
#include "intl.h"
#include "stor-layout.h"
#include "calls.h"
#include "attribs.h"
#include "varasm.h"
#include "trans-mem.h"
#include "../c-family/c-objc.h"
#include "common/common-target.h"
#include "langhooks.h"
#include "tree-inline.h"
#include "ipa-strub.h"
#include "toplev.h"
#include "tree-iterator.h"
#include "opts.h"
#include "gimplify.h"
#include "tree-pretty-print.h"
#include "gcc-rich-location.h"
#include "gcc-urlifier.h"
#include "attr-callback.h"

/* Information required for each thunk in the queue to be emitted. */
struct GTY(()) pending_interceptor_thunk
{
  tree thunk_decl;
  char * GTY((skip)) core_asm_name;
};

/* Global variables needed for [[interceptor]] functions. */
static GTY(()) tree current_interceptor_thunk_decl = NULL_TREE;
static char *current_interceptor_core_asm_name = NULL;
static GTY(()) vec<pending_interceptor_thunk *, va_gc> *pending_interceptor_thunks = NULL;

/* Handle an "interceptor" attribute; arguments as in
   struct attribute_spec.handler.  */

tree
handle_interceptor_attribute (tree *node, tree name, tree ARG_UNUSED (args),
                              int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      fatal_error (node ? DECL_SOURCE_LOCATION (*node) : 0,
                   "%qE attribute only applies to functions", name);
      *no_add_attrs = true;
      return NULL_TREE;
    }

  if (!DECL_INITIAL (*node))
    {
      fatal_error (node ? DECL_SOURCE_LOCATION (*node) : 0,
                "%<[[interceptor]]%> attribute only applies to function definitions");
      *no_add_attrs = true;
      return NULL_TREE;
    }

  if (!cpp_defined (parse_in, (const unsigned char*)"_GLIBCXX_INTERCEPTOR", sizeof ("_GLIBCXX_INTERCEPTOR") - 1u))
    {
      fatal_error ( node ? DECL_SOURCE_LOCATION (*node) : 0,
                   "%<interceptor%> attribute may only be used after including %<<interceptor>%> header");
      *no_add_attrs = true;
      return NULL_TREE;
    }

  tree const fntype = TREE_TYPE (*node);

  if (TREE_CODE (fntype) != FUNCTION_TYPE
      || !VOID_TYPE_P (TREE_TYPE (fntype))
      || TYPE_ARG_TYPES (fntype) != void_list_node)
    {
      error_at (DECL_SOURCE_LOCATION (*node),
                "interceptor function %qD must have type %<void(void)%>",
                *node);
      *no_add_attrs = true;
      return NULL_TREE;
    }

  if (!TYPE_NOTHROW_P (fntype))
    {
      error_at (DECL_SOURCE_LOCATION (*node),
                "interceptor function %qD must be declared %<noexcept%>",
                *node);
      *no_add_attrs = true;
      return NULL_TREE;
    }

  return NULL_TREE;
}

static void
configure_interceptor_core (tree core_decl)
{
  tree target_fn_type;
  tree target_fn_ptr_type;
  tree new_type;
  tree inter_parm;

  gcc_assert (TREE_CODE (core_decl) == FUNCTION_DECL);
  gcc_assert (TREE_CODE (TREE_TYPE (core_decl)) == FUNCTION_TYPE);

  /* Build: void(void) */
  target_fn_type = build_function_type_list (void_type_node, NULL_TREE);

  /* Build: void(*)(void) */
  target_fn_ptr_type = build_pointer_type (target_fn_type);

  /* Build: auto(void *&) -> void(*)(void) */
  new_type = build_function_type_list (target_fn_ptr_type, build_reference_type (ptr_type_node), NULL_TREE);

  /* noexcept(true) */
  new_type = build_exception_variant (new_type, noexcept_true_spec);

  /* Set the calling convention (e.g. x86_32 fastcall) */
  if (!TARGET_64BIT)
    {
      tree attrs = DECL_ATTRIBUTES (core_decl);
      attrs = remove_attribute("cdecl"     , attrs);
      attrs = remove_attribute("fastcall"  , attrs);
      attrs = remove_attribute("regparm"   , attrs);
      attrs = remove_attribute("sseregparm", attrs);
      attrs = remove_attribute("stdcall"   , attrs);
      attrs = remove_attribute("thiscall"  , attrs);
      attrs = tree_cons (get_identifier ("fastcall"), NULL_TREE, attrs);
      DECL_ATTRIBUTES (core_decl) = attrs;
      new_type = build_type_attribute_variant (new_type, attrs);
    }

  TREE_TYPE (core_decl) = new_type;

  /* Hidden first parameter passed to the core.  */
  tree std_id = get_identifier ("std");

  /* Use 'my_std_ns' to prevent macro collision with GCC's internal 'std_node' */
  tree my_std_ns = get_namespace_binding (global_namespace, std_id);

  tree detail_id = get_identifier ("interceptor_detail");
  tree my_detail_ns = my_std_ns ? get_namespace_binding (my_std_ns, detail_id) : NULL_TREE;

  tree frame_id = get_identifier ("Frame");
  tree my_frame_decl = my_detail_ns ? get_namespace_binding (my_detail_ns, frame_id) : NULL_TREE;

  /* Extract the actual RECORD_TYPE from the TYPE_DECL. */
  tree frame_type = my_frame_decl ? TREE_TYPE (my_frame_decl) : NULL_TREE;

  /* 2. Build the "std::interceptor_detail::Frame &" type.
        We use a defensive fallback to void*& (ptr_type_node) if lookup fails
        so the compiler emits an error instead of suffering an ICE (crash). */
  tree frame_ref_type;
  if (frame_type && frame_type != error_mark_node)
    {
      frame_ref_type = build_reference_type (frame_type);
    }
  else
    {
      error_at (DECL_SOURCE_LOCATION (core_decl),
                "could not find %<std::interceptor_detail::Frame%>; "
                "did you forget to include the corresponding header?");
      /* Fallback to void*& to allow the compiler to exit gracefully. */
      frame_ref_type = build_reference_type (ptr_type_node);
    }

  /* 3. Hidden first parameter passed to the core.  */
  inter_parm = build_decl (DECL_SOURCE_LOCATION (core_decl),
                           PARM_DECL,
                           get_identifier ("__inter"),
                           frame_ref_type);
  DECL_CONTEXT   (inter_parm) = core_decl;
  TREE_USED      (inter_parm) = 1;
  DECL_READ_P    (inter_parm) = 1;
  DECL_ARGUMENTS (core_decl) = inter_parm;

  if (DECL_RESULT (core_decl) != NULL_TREE)
    {
      TREE_TYPE    (DECL_RESULT (core_decl)) = target_fn_ptr_type;
      DECL_CONTEXT (DECL_RESULT (core_decl)) = core_decl         ;
    }
}

void
start_function_interceptor (tree *pdecl1)
{
  tree core_decl;
  const char *thunk_name;
  char *core_name;

  gcc_assert (*pdecl1 != NULL_TREE);

  gcc_assert (current_interceptor_thunk_decl    == NULL_TREE);
  gcc_assert (current_interceptor_core_asm_name == NULL     );

  current_interceptor_thunk_decl = *pdecl1;

  /* Keep the public decl visible as the declaration.  */
  mangle_decl (current_interceptor_thunk_decl);
  pushdecl (current_interceptor_thunk_decl);

  /* Make separate core decl.  */
  core_decl = copy_node (*pdecl1);
  DECL_LANG_SPECIFIC (core_decl) = DECL_LANG_SPECIFIC (*pdecl1);
  cxx_dup_lang_specific_decl (core_decl);
  DECL_ATTRIBUTES (core_decl) = remove_attribute ("interceptor", DECL_ATTRIBUTES (core_decl));
  DECL_INTERCEPTOR_P (core_decl) = 0;
  configure_interceptor_core (core_decl);

  /* Save original/public symbol name.  */
  mangle_decl (*pdecl1);
  thunk_name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (*pdecl1));

  /* Compute core mangled name.  */
  core_name = XNEWVEC (char, 7 + strlen (thunk_name) + 1);
  strcpy (core_name, "__core_" );
  strcat (core_name, thunk_name);

  /* Give only the copied core decl the new assembler name.  */
#if 0
  overwrite_mangling (core_decl, get_identifier (core_name));
  record_mangling (core_decl, true);
#else
  SET_DECL_ASSEMBLER_NAME (core_decl, get_identifier (core_name));
  DECL_NAME (core_decl) = get_identifier (core_name);
#endif

  current_interceptor_core_asm_name = core_name;

  /* Compile body into core decl.  */
  *pdecl1 = core_decl;
}

static tree
emit_pure_assembly_as_function (const char *mangled_name,
                                tree model_decl,
                                const char *assembly)
{
  location_t loc = 0;
  tree asm_string;
  char *buf;
  int public_p = 1;

  gcc_assert (mangled_name != NULL);
  gcc_assert (assembly != NULL);

  if (model_decl != NULL_TREE)
    {
      gcc_assert (TREE_CODE (model_decl) == FUNCTION_DECL);
      loc = DECL_SOURCE_LOCATION (model_decl);
      public_p = TREE_PUBLIC (model_decl);
    }

  const char *linkage_directive = public_p ? ".globl " : "";

#if defined(TARGET_PECOFF) && (0 != TARGET_PECOFF)  /* MS-Windows uses PE/COFF executable files */
    {
      buf = XNEWVEC (char,
                     strlen (".section .text$") + strlen (mangled_name) + strlen(",\"x\"\n")
                     + strlen (".linkonce discard\n")
                     + strlen (linkage_directive)
                     + strlen (mangled_name)
                     + strlen ("\n")
                     + strlen (mangled_name)
                     + strlen (":\n")
                     + strlen (assembly)
                     + strlen ("\n")
                     + 1);

      sprintf (buf,
               ".section .text$%s,\"x\"\n"
               ".linkonce discard\n"
               "%s%s\n"
               "%s:\n"
               "%s\n",
               mangled_name,
               linkage_directive, mangled_name,
               mangled_name,
               assembly);
    }
#else    /* Linux uses PE/COFF executable files */
    {
      buf = XNEWVEC (char,
                     strlen (linkage_directive)
                     + strlen (mangled_name)
                     + strlen ("\n.weak ") + strlen (mangled_name)
                     + strlen ("\n.type , @function\n:\n\n.size , .-\n")
                     + strlen (mangled_name) * 4
                     + strlen (assembly)
                     + 1);

      sprintf (buf,
               "%s%s\n"
               ".weak %s\n"
               ".type %s, @function\n"
               "%s:\n"
               "%s\n"
               ".size %s, .-%s\n",
               linkage_directive, mangled_name,
               mangled_name,
               mangled_name,
               mangled_name,
               assembly,
               mangled_name, mangled_name);
    }
#endif  // if PE or ELF

  asm_string = build_string (strlen (buf) + 1, buf);

  finish_asm_stmt (loc,
                   /*volatile_p=*/1,
                   asm_string,
                   /*output_operands=*/NULL_TREE,
                   /*input_operands=*/NULL_TREE,
                   /*clobbers=*/NULL_TREE,
                   /*labels=*/NULL_TREE,
                   /*inline_p=*/false,
                   /*toplev_p=*/true);

  XDELETEVEC (buf);
  return NULL_TREE;
}

enum BaseArchitecture { BaseArchitecture_Unspecified, BaseArchitecture_x86, BaseArchitecture_ARM };

template<BaseArchitecture A = BaseArchitecture_Unspecified, unsigned ptr_size = 0u>
static char *
get_assembler_for_interceptor_thunk (const char *core_name);

template<>
char *
get_assembler_for_interceptor_thunk<BaseArchitecture_x86, 32u> (const char *core_name)
{
  constexpr char front[] =
    "pushl %%eax\n"
    "pushl %%edx\n"
    "pushl %%ecx\n"
    "pushl %%ebx\n"

    "movl $1, %%eax\n"
    "cpuid\n"

    "bt $25, %%edx\n"
    "jnc 3f\n"

    "bt $27, %%ecx\n"
    "jnc 1f\n"
    "bt $28, %%ecx\n"
    "jnc 1f\n"

    "xorl %%ecx, %%ecx\n"
    "xgetbv\n"
    "andl $0x6, %%eax\n"
    "cmpl $0x6, %%eax\n"
    "je 2f\n"

    "1:\n"
    "popl %%ebx\n"
    "subl $48, %%esp\n"
    "movdqu %%xmm0, 0(%%esp)\n"
    "movdqu %%xmm1, 16(%%esp)\n"
    "movdqu %%xmm2, 32(%%esp)\n"
    "leal 64(%%esp), %%ecx\n"  // assuming the target is __cdecl
    "call ";

  constexpr char middle[] =
    "\n"
    "movl %%eax, %%esi\n"
    "movdqu 0(%%esp), %%xmm0\n"
    "movdqu 16(%%esp), %%xmm1\n"
    "movdqu 32(%%esp), %%xmm2\n"
    "addl $48, %%esp\n"
    "popl %%ecx\n"
    "popl %%edx\n"
    "popl %%eax\n"
    "jmp *%%esi\n"

    "2:\n"
    "popl %%ebx\n"
    "subl $96, %%esp\n"
    "vmovdqu %%ymm0, 0(%%esp)\n"
    "vmovdqu %%ymm1, 32(%%esp)\n"
    "vmovdqu %%ymm2, 64(%%esp)\n"
    "leal 112(%%esp), %%ecx\n"  // assuming the target is __cdecl
    "call ";

  constexpr char back[] =
    "\n"
    "movl %%eax, %%esi\n"
    "vmovdqu 0(%%esp), %%ymm0\n"
    "vmovdqu 32(%%esp), %%ymm1\n"
    "vmovdqu 64(%%esp), %%ymm2\n"
    "addl $96, %%esp\n"
    "popl %%ecx\n"
    "popl %%edx\n"
    "popl %%eax\n"
    "jmp *%%esi\n"

    "3:\n"
    "popl %%ebx\n"
    "leal 16(%%esp), %%ecx\n"  // assuming the target is __cdecl
    "call ";

  constexpr char tail[] =
    "\n"
    "movl %%eax, %%esi\n"
    "popl %%ecx\n"
    "popl %%edx\n"
    "popl %%eax\n"
    "jmp *%%esi\n";

  char *p = XNEWVEC (char,
                     sizeof (front) - 1u
                     + strlen (core_name)
                     + sizeof (middle) - 1u
                     + strlen (core_name)
                     + sizeof (back) - 1u
                     + strlen (core_name)
                     + sizeof (tail) - 1u
                     + 1u);

  strcpy (p, front);
  strcat (p, core_name);
  strcat (p, middle);
  strcat (p, core_name);
  strcat (p, back);
  strcat (p, core_name);
  strcat (p, tail);

  return p;
}

template<>
char *
get_assembler_for_interceptor_thunk<BaseArchitecture_x86, 64u> (const char *core_name)
{
#if defined(TARGET_PECOFF) && (0 != TARGET_PECOFF)
  #define FIRST_ARG_REGISTER "%%rcx"
#else
  #define FIRST_ARG_REGISTER "%%rdi"
#endif

#define PUSH_ALL_INTS    \
    "pushq %%rdi\n"      \
    "pushq %%rsi\n"      \
    "pushq %%rdx\n"      \
    "pushq %%rcx\n"      \
    "pushq %%r8\n"       \
    "pushq %%r9\n"       \
    "pushq %%rax\n"

#define POP_ALL_INTS     \
    "popq %%rax\n"       \
    "popq %%r9\n"        \
    "popq %%r8\n"        \
    "popq %%rcx\n"       \
    "popq %%rdx\n"       \
    "popq %%rsi\n"       \
    "popq %%rdi\n"

#define PUSH_ALL_FLOATS_CLOBBER_R11          \
    "leaq 666f(%%rip), %%r11\n"              \
    "pushq %%r11\n"                          \
    "jmp __interceptor_vector_pusher\n"      \
    "666:\n"

#define POP_ALL_FLOATS_CLOBBER_R11           \
    "leaq 667f(%%rip), %%r11\n"              \
    "pushq %%r11\n"                          \
    "jmp __interceptor_vector_popper\n"      \
    "667:\n"

#define PUSH_ALL_CLOBBER_R11          \
    PUSH_ALL_INTS                     \
    PUSH_ALL_FLOATS_CLOBBER_R11

#define POP_ALL_CLOBBER_R11           \
    POP_ALL_FLOATS_CLOBBER_R11        \
    POP_ALL_INTS

#define ALIGN_STACK_FRONT    \
    "pushq %%rsp\n"          \
    "pushq (%%rsp)\n"        \
    "andq  $-16, %%rsp\n"

#define ALIGN_STACK_BACK     \
    "addq  $8, %%rsp\n"      \
    "popq  %%rsp\n"

/* When performing a function call before the target function has
 * been jumped to, we must preserve all registers that might contain
 * arguments. */
#define SAFE_CALL_BEFORE_TARGET(name_of_function, argument, name_of_retval_register)  \
    PUSH_ALL_CLOBBER_R11                                      \
    ALIGN_STACK_FRONT                                         \
    "movq " argument ", " FIRST_ARG_REGISTER "\n"             \
    "subq $32, %%rsp\n"  /* msabi shadow stack space */       \
    "call " name_of_function "\n"                             \
    "addq $32, %%rsp\n"  /* msabi shadow stack space */       \
    ALIGN_STACK_BACK                                          \
    "movq %%rax, " name_of_retval_register "\n"               \
    POP_ALL_CLOBBER_R11

/* When performing a function call after the target function has
 * been jumped to, we only need to preserve registers that might
 * contain return values. */
#define SAFE_CALL_AFTER_TARGET(name_of_function, argument, name_of_retval_register)  \
    PUSH_ALL_CLOBBER_R11                                      \
    "movq " argument ", " FIRST_ARG_REGISTER "\n"             \
    ALIGN_STACK_FRONT                                         \
    "subq $32, %%rsp\n"  /* msabi shadow stack space */       \
    "call " name_of_function "\n"                             \
    "addq $32, %%rsp\n"  /* msabi shadow stack space */       \
    ALIGN_STACK_BACK                                          \
    "movq %%rax, " name_of_retval_register "\n"               \
    POP_ALL_CLOBBER_R11

  constexpr char front[] =
    "pushq $0\n"     /* set 'outward' address = nullptr */
    "leaq (%%rsp), %%r10\n"    /* Prepare 'outward' address */
    PUSH_ALL_CLOBBER_R11
    "movq %%r10, " FIRST_ARG_REGISTER "\n"      /* Pass the address of 'outward' variable to the core */
    ALIGN_STACK_FRONT
    "subq $32, %%rsp\n"    /* 32 bytes stack shadow space for Microsoft -- harmless for Linux */
    "call ";  //===================================================================================================<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

constexpr char back[] =
    "\n"
    "addq $32, %%rsp\n"     /* 32 bytes stack shadow space for Microsoft -- harmless for Linux */
    ALIGN_STACK_BACK
    "movq %%rax, %%r10\n"   /* Preserve the target function address */
    POP_ALL_CLOBBER_R11
    "popq %%r11\n"          // retrieve 'outward' address from top of stack
    "cmpq $0, %%r11\n"      // Check if the 'outward' address is a nullptr
    "jne 1f\n"
    // control reaches here if there is no outward interception
    "jmp *%%r10\n"       /* Jump straight to the target without outward interception */

"1:\n"  /* has_outward */
    // When control reaches here:
    //  - 'outward' address is in r11
    //  - 'target'  address is in r10
    // There is nothing extra on the stack, so return address is at 0(%%rsp)
    "pushq %%r13\n"             // Callee-saved register -- only saving to restore later
    "movq %%rsp, %%r13\n"
    "pushq %%r12\n"             // Callee-saved register -- only saving to restore later
    "movq 16(%%rsp), %%r12\n"   // retrieve original return address from stack
    "pushq %%r10\n"  // scratch - target  address
    "pushq %%r11\n"  // scratch - outward address
    SAFE_CALL_BEFORE_TARGET( "__interceptor_stashed_addresses", "%%r13", "%%r13" )       // ------- for storing
    "popq %%r11\n"               // restore after function call - outward address
    "movq %%r12, 0(%%r13)\n"     // Store original return address in addresses[0]
    "movq %%r11, 8(%%r13)\n"     // Store the   'outward' address in addresses[1]
    "popq  %%r10\n"  // restore after function call - target address
    "popq  %%r12\n"  // restore callee-saved register
    "popq  %%r13\n"  // restore callee-saved register

    "leaq 2f(%%rip), %%r11\n"  // prepare the new revised return address
    "movq %%r11, (%%rsp)\n"     // edit the return address at top of stack
    "jmp *%%r10\n"              // jump to the target function and jump back to manipluated return address

"2:\n"  // new_return_address
    "pushq %%r14\n"
    "pushq %%r13\n"
    "movq %%rsp, %%r13\n"
    "addq $1, %%r13\n"
    "pushq %%r12\n"
    SAFE_CALL_AFTER_TARGET( "__interceptor_stashed_addresses", "%%r13", "%%r12" )       // ------- for restoring
    "movq 0(%%r12), %%r13\n" // original return address
    "movq 8(%%r12), %%r14\n" // address of outward interceptor
    SAFE_CALL_AFTER_TARGET( "*%%r14", "$0",  "%%rax" )   // call the 'outward' interceptor (deliberately discard return value)
    "movq %%r13, %%r11\n"  // original return address
    "popq %%r12\n"         // restore callee-saved register
    "popq %%r13\n"         // restore callee-saved register
    "popq %%r14\n"         // restore callee-saved register
    "jmp *%%r11\n";        // jump back to original return address

  char *const p = XNEWVEC (char, sizeof(front)-1u + strlen (core_name) + sizeof(back)-1u + 1u);
  strcpy (p, front    );
  strcat (p, core_name);
  strcat (p, back     );
  return p;

#undef SAFE_CALL_AFTER_TARGET
#undef SAFE_CALL_BEFORE_TARGET
#undef ALIGN_STACK_BACK
#undef ALIGN_STACK_FRONT
#undef POP_ALL_CLOBBER_R11
#undef PUSH_ALL_CLOBBER_R11
#undef POP_ALL_FLOATS_CLOBBER_R11
#undef PUSH_ALL_FLOATS_CLOBBER_R11
#undef POP_ALL_INTS
#undef PUSH_ALL_INTS
#undef FIRST_ARG_REGISTER
}

template<>
char *
get_assembler_for_interceptor_thunk<BaseArchitecture_Unspecified,0u> (const char *core_name)
{
  if ( TARGET_64BIT ) return get_assembler_for_interceptor_thunk< BaseArchitecture_x86, 64u >(core_name);
  /************/ else return get_assembler_for_interceptor_thunk< BaseArchitecture_x86, 32u >(core_name);
}

void
queue_emission_of_interceptor_thunk_if_decl_is_interceptor (tree fndecl)
{
  if (current_interceptor_thunk_decl       == NULL_TREE) return;
  if (current_interceptor_core_asm_name    == NULL     ) return;
  if (current_interceptor_core_asm_name[0] == '\0'     ) return;
  if (fndecl                               == NULL_TREE) return;

  if (!DECL_ASSEMBLER_NAME_SET_P (current_interceptor_thunk_decl)) return;
  if (!DECL_ASSEMBLER_NAME_SET_P (fndecl                        )) return;

  if (0 != strcmp (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (fndecl)), current_interceptor_core_asm_name)) return;

  pending_interceptor_thunk *q = ggc_cleared_alloc<pending_interceptor_thunk> ();

  q->thunk_decl    = current_interceptor_thunk_decl   ;
  q->core_asm_name = current_interceptor_core_asm_name;

  vec_safe_push (pending_interceptor_thunks, q);

  current_interceptor_thunk_decl    = NULL_TREE;
  current_interceptor_core_asm_name = NULL     ;
}

static void emit_vector_pusher(void)
{
  emit_pure_assembly_as_function ("__interceptor_vector_pusher", NULL_TREE,
"    mov __interceptor_xmm_ymm_zmm(%%rip), %%r11\n"
"    cmp $1, %%r11\n"
"    je 1f\n"
"    cmp $2, %%r11\n"
"    je 2f\n"
"    cmp $3, %%r11\n"
"    je 3f\n"
"    ud2\n"
"2:\n"
"    pop %%r11\n"
"    subq $256, %%rsp\n"
"    vmovdqu %%ymm0,   0(%%rsp)\n"
"    vmovdqu %%ymm1,  32(%%rsp)\n"
"    vmovdqu %%ymm2,  64(%%rsp)\n"
"    vmovdqu %%ymm3,  96(%%rsp)\n"
"    vmovdqu %%ymm4, 128(%%rsp)\n"
"    vmovdqu %%ymm5, 160(%%rsp)\n"
"    vmovdqu %%ymm6, 192(%%rsp)\n"
"    vmovdqu %%ymm7, 224(%%rsp)\n"
"    jmp *%%r11\n"
"3:\n"
"    pop %%r11\n"
"    subq $576, %%rsp\n"
"    vmovdqu64 %%zmm0,   0(%%rsp)\n"
"    vmovdqu64 %%zmm1,  64(%%rsp)\n"
"    vmovdqu64 %%zmm2, 128(%%rsp)\n"
"    vmovdqu64 %%zmm3, 192(%%rsp)\n"
"    vmovdqu64 %%zmm4, 256(%%rsp)\n"
"    vmovdqu64 %%zmm5, 320(%%rsp)\n"
"    vmovdqu64 %%zmm6, 384(%%rsp)\n"
"    vmovdqu64 %%zmm7, 448(%%rsp)\n"
"    kmovq %%k0, 512(%%rsp)\n"
"    kmovq %%k1, 520(%%rsp)\n"
"    kmovq %%k2, 528(%%rsp)\n"
"    kmovq %%k3, 536(%%rsp)\n"
"    kmovq %%k4, 544(%%rsp)\n"
"    kmovq %%k5, 552(%%rsp)\n"
"    kmovq %%k6, 560(%%rsp)\n"
"    kmovq %%k7, 568(%%rsp)\n"
"    jmp *%%r11\n"
"1:\n"
"    pop %%r11\n"
"    subq $128, %%rsp\n"
"    movdqu %%xmm0,   0(%%rsp)\n"
"    movdqu %%xmm1,  16(%%rsp)\n"
"    movdqu %%xmm2,  32(%%rsp)\n"
"    movdqu %%xmm3,  48(%%rsp)\n"
"    movdqu %%xmm4,  64(%%rsp)\n"
"    movdqu %%xmm5,  80(%%rsp)\n"
"    movdqu %%xmm6,  96(%%rsp)\n"
"    movdqu %%xmm7, 112(%%rsp)\n"
"    jmp *%%r11\n");
}

static void emit_vector_popper(void)
{
  emit_pure_assembly_as_function ("__interceptor_vector_popper", NULL_TREE,
"    mov __interceptor_xmm_ymm_zmm(%%rip), %%r11\n"
"    cmp $1, %%r11\n"
"    je 1f\n"
"    cmp $2, %%r11\n"
"    je 2f\n"
"    cmp $3, %%r11\n"
"    je 3f\n"
"    ud2\n"
"2:\n"
"    pop %%r11\n"
"    vmovdqu   0(%%rsp), %%ymm0\n"
"    vmovdqu  32(%%rsp), %%ymm1\n"
"    vmovdqu  64(%%rsp), %%ymm2\n"
"    vmovdqu  96(%%rsp), %%ymm3\n"
"    vmovdqu 128(%%rsp), %%ymm4\n"
"    vmovdqu 160(%%rsp), %%ymm5\n"
"    vmovdqu 192(%%rsp), %%ymm6\n"
"    vmovdqu 224(%%rsp), %%ymm7\n"
"    addq $256, %%rsp\n"
"    jmp *%%r11\n"
"3:\n"
"    pop %%r11\n"
"    vmovdqu64   0(%%rsp), %%zmm0\n"
"    vmovdqu64  64(%%rsp), %%zmm1\n"
"    vmovdqu64 128(%%rsp), %%zmm2\n"
"    vmovdqu64 192(%%rsp), %%zmm3\n"
"    vmovdqu64 256(%%rsp), %%zmm4\n"
"    vmovdqu64 320(%%rsp), %%zmm5\n"
"    vmovdqu64 384(%%rsp), %%zmm6\n"
"    vmovdqu64 448(%%rsp), %%zmm7\n"
"    kmovq 512(%%rsp), %%k0\n"
"    kmovq 520(%%rsp), %%k1\n"
"    kmovq 528(%%rsp), %%k2\n"
"    kmovq 536(%%rsp), %%k3\n"
"    kmovq 544(%%rsp), %%k4\n"
"    kmovq 552(%%rsp), %%k5\n"
"    kmovq 560(%%rsp), %%k6\n"
"    kmovq 568(%%rsp), %%k7\n"
"    addq $576, %%rsp\n"
"    jmp *%%r11\n"
"1:\n"
"    pop %%r11\n"
"    movdqu   0(%%rsp), %%xmm0\n"
"    movdqu  16(%%rsp), %%xmm1\n"
"    movdqu  32(%%rsp), %%xmm2\n"
"    movdqu  48(%%rsp), %%xmm3\n"
"    movdqu  64(%%rsp), %%xmm4\n"
"    movdqu  80(%%rsp), %%xmm5\n"
"    movdqu  96(%%rsp), %%xmm6\n"
"    movdqu 112(%%rsp), %%xmm7\n"
"    addq $128, %%rsp\n"
"    jmp *%%r11\n"
);
}

static void emit_xmm_ymm_zmm_checker(void)
{
  emit_pure_assembly_as_function ("__interceptor_xmm_ymm_zmm_Checker_ASM", NULL_TREE,
    /* Detect SSE-128 / AVX-256 / AVX-512 */
"    push    %%rbx\n"
"    movl    $1, %%eax\n"         // Default result: SSE
"    cpuid\n"                     // Check OSXSAVE/AVX support
"    bt      $27, %%ecx\n"
"    setc    %%r8b\n"             // r8b = 1 if OSXSAVE
"    bt      $28, %%ecx\n"
"    setc    %%r9b\n"             // r9b = 1 if AVX
"    and     %%r8b, %%r9b\n"      // r9b = OSXSAVE AND AVX
"    test    %%r9b, %%r9b\n"
"    jnz     .+4\n"
"    pop     %%rbx\n"
"    ret\n"                       // If not AVX, return 1
"    xorl    %%ecx, %%ecx\n"
"    xgetbv\n"
"    movl    %%eax, %%r10d\n"
"    andl    $0x6, %%r10d\n"
"    cmpl    $0x6, %%r10d\n"
"    setz    %%r8b\n"             // r8b = 1 if XMM/YMM enabled
"    test    %%r8b, %%r8b\n"
"    jnz     .+4\n"
"    pop     %%rbx\n"
"    ret\n"                       // If AVX not enabled, return 1
"    movl    $7, %%eax\n"
"    xorl    %%ecx, %%ecx\n"
"    cpuid\n"
"    bt      $16, %%ebx\n"
"    setc    %%r8b\n"             // r8b = 1 if AVX-512F
"    test    %%r8b, %%r8b\n"
"    jnz     .+9\n"
"    movl    $2, %%eax\n"
"    pop     %%rbx\n"
"    ret\n"                       // If AVX enable but not AVX-512F, return 2 for AVX-256
"    xorl    %%ecx, %%ecx\n"
"    xgetbv\n"
"    andl    $0xE6, %%eax\n"
"    cmpl    $0xE6, %%eax\n"
"    movl    $2, %%eax\n"         // Default here: AVX
"    movl    $3, %%r10d\n"        // AVX-512
"    cmove   %%r10d, %%eax\n"     // If AVX-512 state enabled, return 3
"    pop     %%rbx\n"
"    ret\n");
}

void
flush_pending_interceptor_thunks (void)
{
  if (cpp_defined (parse_in, (const unsigned char*)"_GLIBCXX_INTERCEPTOR", sizeof ("_GLIBCXX_INTERCEPTOR") - 1u))
    {
      emit_xmm_ymm_zmm_checker ();
      emit_vector_pusher ();
      emit_vector_popper ();
    }

  if (!pending_interceptor_thunks)
    return;

  for (unsigned i = 0; i < pending_interceptor_thunks->length (); ++i)
    {
      pending_interceptor_thunk *q = (*pending_interceptor_thunks)[i];
      char *asm_text = get_assembler_for_interceptor_thunk (q->core_asm_name);

      emit_pure_assembly_as_function (q->core_asm_name + strlen ("__core_"), q->thunk_decl, asm_text);

      XDELETEVEC (asm_text        );
      XDELETEVEC (q->core_asm_name);
      q->core_asm_name = NULL;
    }

  pending_interceptor_thunks = NULL;
}

tree
build_interceptor_goto_target (location_t loc, tree expr)
{
  tree target_fn_type  = build_function_type_list (void_type_node, NULL_TREE);
  tree target_ptr_type = build_pointer_type (target_fn_type);

  /* First try a C-style cast.  */
  tree converted = build_c_cast (loc, target_ptr_type, expr);
  if (converted != error_mark_node)
    return converted;

  /* Maybe also try a bit_cast here if the size and alignment match */

  error_at (loc, "operand of %<goto ->%> is not castable to %<void (*)(void)%>");

  return error_mark_node;
}

#include "gt-cp-interceptor.h"
