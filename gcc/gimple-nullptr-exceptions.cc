/* Throw exceptions on null pointer dereference and null pointer arithmetic.
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

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* This pass implements -fnullptr-exceptions.  Every dereference of a
   pointer that cannot be proved non-null is preceded by a test against
   the null pointer; when the test fires, the pass emits a call to
   __cxa_throw_null_pointer_dereference, which throws std::nullptr_error.
   Pointer arithmetic that adds a non-zero offset to a null pointer is
   handled the same way, through __cxa_throw_null_pointer_arithmetic.

   The pass runs in the lowering pipeline, immediately after pass_lower_cf
   and, critically, *before* pass_lower_eh.  The call it inserts can throw,
   so it has to be in place when lower_eh_constructs walks the body: that
   is what puts the call in the EH region enclosing the guarded statement,
   and hence what makes the cleanups of that region -- C++ destructors,
   __attribute__ ((cleanup)) handlers in C -- run as the exception
   propagates.

   The existing -fsanitize=null instrumentation in ubsan.cc cannot be
   reused for this reason.  pass_ubsan runs inside pass_build_ssa_passes,
   long after EH lowering; a handler that aborts or returns does not care,
   but a handler that throws from there would unwind straight past every
   local cleanup in the function.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "fold-const.h"
#include "gimple-iterator.h"
#include "gimple-walk.h"
#include "gimplify.h"
#include "builtins.h"
#include "cgraph.h"
#include "stringpool.h"
#include "varasm.h"
#include "diagnostic-core.h"

/* Return the runtime entry point FNCODE, or NULL_TREE if the front end
   did not make it available.  */

static tree
nullptr_exc_decl (enum built_in_function fncode)
{
  tree decl = builtin_decl_explicit (fncode);
  if (decl == NULL_TREE)
    return NULL_TREE;

  /* These entry points exist in order to throw, so they must not be
     treated as nothrow no matter what else is in effect.  */
  TREE_NOTHROW (decl) = 0;
  return decl;
}

/* Force VAL into something usable as a GIMPLE_COND operand, inserting any
   load before the statement GSI points at.  Return NULL_TREE if that is
   not possible.  */

static tree
force_cond_operand (gimple_stmt_iterator *gsi, tree val, location_t loc)
{
  if (is_gimple_val (val))
    return val;
  if (!DECL_P (val))
    return NULL_TREE;

  tree tmp = create_tmp_reg (TREE_TYPE (val));
  gassign *load = gimple_build_assign (tmp, val);
  gimple_set_location (load, loc);
  gsi_insert_before (gsi, load, GSI_SAME_STMT);
  return tmp;
}

/* Emit, immediately before the statement GSI points at:

	if (PTR != 0) goto done;
	if (GUARD1 == 0) goto done;	// only for the guards that are given
	if (GUARD2 == 0) goto done;
	__cxa_throw_null_pointer_*();
      done:

   so the throw fires only when PTR is null and every guard given is
   non-zero.  The guards carry the conditional forms of nonnull, where a
   pointer argument only has to be non-null when some size argument is
   non-zero.

   LOC is used for every inserted statement, so that the exception is
   attributed to the offending expression.  */

static void
emit_guarded_throw (gimple_stmt_iterator *gsi, location_t loc, tree ptr,
		    tree guard1, tree guard2, enum built_in_function fncode)
{
  tree fndecl = nullptr_exc_decl (fncode);
  if (fndecl == NULL_TREE)
    return;

  ptr = force_cond_operand (gsi, ptr, loc);
  if (ptr == NULL_TREE)
    return;

  tree guards[2] = { guard1, guard2 };
  unsigned int nguards = 0;
  for (unsigned int i = 0; i < 2; ++i)
    if (guards[i] != NULL_TREE)
      {
	tree g = force_cond_operand (gsi, guards[i], loc);
	if (g == NULL_TREE)
	  return;
	guards[nguards++] = g;
      }

  tree lab_throw = create_artificial_label (loc);
  tree lab_done = create_artificial_label (loc);
  tree lab_guard[2] = { NULL_TREE, NULL_TREE };
  for (unsigned int i = 0; i < nguards; ++i)
    lab_guard[i] = create_artificial_label (loc);

  gcond *cond = gimple_build_cond (NE_EXPR, ptr,
				   build_zero_cst (TREE_TYPE (ptr)),
				   lab_done,
				   nguards ? lab_guard[0] : lab_throw);
  gimple_set_location (cond, loc);
  gsi_insert_before (gsi, cond, GSI_SAME_STMT);

  for (unsigned int i = 0; i < nguards; ++i)
    {
      gsi_insert_before (gsi, gimple_build_label (lab_guard[i]),
			 GSI_SAME_STMT);
      cond = gimple_build_cond (EQ_EXPR, guards[i],
				build_zero_cst (TREE_TYPE (guards[i])),
				lab_done,
				i + 1 < nguards ? lab_guard[i + 1]
						: lab_throw);
      gimple_set_location (cond, loc);
      gsi_insert_before (gsi, cond, GSI_SAME_STMT);
    }

  gsi_insert_before (gsi, gimple_build_label (lab_throw), GSI_SAME_STMT);

  gcall *call = gimple_build_call (fndecl, 0);
  gimple_set_location (call, loc);
  gsi_insert_before (gsi, call, GSI_SAME_STMT);

  gsi_insert_before (gsi, gimple_build_label (lab_done), GSI_SAME_STMT);
}

/* Guard the dereference in T, if T contains one.  */

static void
instrument_deref (gimple_stmt_iterator *gsi, tree t, location_t loc)
{
  /* &p->field computes an address, it does not load through one; that is
     pointer arithmetic and is handled separately.  */
  if (t == NULL_TREE || TREE_CODE (t) == ADDR_EXPR)
    return;

  tree base = get_base_address (t);
  if (base == NULL_TREE || TREE_CODE (base) != MEM_REF)
    return;

  /* Some address spaces have a valid object at address zero.  */
  addr_space_t as = TYPE_ADDR_SPACE (TREE_TYPE (base));
  if (!ADDR_SPACE_GENERIC_P (as)
      && targetm.addr_space.zero_address_valid (as))
    return;

  tree ptr = TREE_OPERAND (base, 0);
  if (!POINTER_TYPE_P (TREE_TYPE (ptr)) || TREE_CODE (ptr) == ADDR_EXPR)
    return;

  emit_guarded_throw (gsi, loc, ptr, NULL_TREE, NULL_TREE,
		      BUILT_IN_CXA_THROW_NULL_POINTER_DEREFERENCE);
}

/* Guard every argument of the call at GSI that its callee declares
   nonnull.  This is what covers the C library: glibc declares strcpy with
   __nonnull ((1, 2)) and GCC's own builtin table says the same, so
   strcpy (0, s) throws in the caller's frame before control ever reaches
   the hand-written assembly, which could neither be instrumented nor
   thrown out of.  */

static void
instrument_nonnull_args (gimple_stmt_iterator *gsi, location_t loc)
{
  gimple *stmt = gsi_stmt (*gsi);

  /* infer_nonnull_range_by_attribute needs flag_delete_null_pointer_checks
     set, and -fnullptr-exceptions deliberately clears it.  */
  int save = flag_delete_null_pointer_checks;
  flag_delete_null_pointer_checks = 1;

  /* Skip the implicit object argument of a non-static member function.  It
     is nonnull by the language rules, but a null one is already undefined
     at the call, before anything has been dereferenced.  Checking it here
     puts a throw in front of a base class constructor, and the cleanup
     that unwinding then runs would destroy a base subobject through the
     very pointer that was null.  The callee throws from its own first
     dereference instead, which leaves no half-built object behind.  */
  unsigned int first = 0;
  if (tree fndecl = gimple_call_fndecl (stmt))
    if (TREE_CODE (TREE_TYPE (fndecl)) == METHOD_TYPE)
      first = 1;

  unsigned int nargs = gimple_call_num_args (stmt);
  for (unsigned int i = first; i < nargs; ++i)
    {
      tree arg = gimple_call_arg (stmt, i);
      tree guard1, guard2;

      if (!POINTER_TYPE_P (TREE_TYPE (arg))
	  || TREE_CODE (arg) == ADDR_EXPR)
	continue;
      if (!infer_nonnull_range_by_attribute (stmt, arg, &guard1, &guard2))
	continue;

      if (guard1 == guard2)
	guard2 = NULL_TREE;
      emit_guarded_throw (gsi, loc, arg, guard1, guard2,
			  BUILT_IN_CXA_THROW_NULL_POINTER_ARGUMENT);
    }

  flag_delete_null_pointer_checks = save;
}

/* Guard the pointer arithmetic PTR p+ OFF.  */

static void
instrument_pointer_plus (gimple_stmt_iterator *gsi, tree ptr, tree off,
			 location_t loc)
{
  if (TREE_CODE (ptr) == ADDR_EXPR)
    return;

  /* Adding zero to a null pointer is well defined in C++ and in C23, and
     ordinary code relies on it -- an empty range whose two ends are both
     null, for one -- so only a non-zero offset is a violation.  */
  if (integer_zerop (off))
    return;

  /* A known non-zero offset leaves only the pointer to test.  */
  if (TREE_CODE (off) == INTEGER_CST)
    off = NULL_TREE;

  emit_guarded_throw (gsi, loc, ptr, off, NULL_TREE,
		      BUILT_IN_CXA_THROW_NULL_POINTER_ARITHMETIC);
}

/* C library functions we provide a checked entry point for, and the name
   of that entry point.  Taking the address of one of these yields the
   address of the stub instead, so that a call through the pointer is
   checked just as a direct call would be.

   The set is deliberately confined to library functions: a function
   compiled as part of this program already checks inside its own body, so
   a stub would only repeat the work and, worse, would change the address
   of the user's own functions.  Whether a stub applies is a property of
   the declaration alone, so every translation unit agrees, and there is a
   single definition of each stub in the runtime, so two translation units
   taking the same address still compare equal.  */

struct nullex_stub_entry
{
  enum built_in_function code;
  const char *name;
};

static const nullex_stub_entry nullex_stub_table[] =
{
  { BUILT_IN_MEMCPY,	"__nullex_memcpy" },
  { BUILT_IN_MEMMOVE,	"__nullex_memmove" },
  { BUILT_IN_MEMSET,	"__nullex_memset" },
  { BUILT_IN_MEMCMP,	"__nullex_memcmp" },
  { BUILT_IN_MEMCHR,	"__nullex_memchr" },
  { BUILT_IN_STRCPY,	"__nullex_strcpy" },
  { BUILT_IN_STRNCPY,	"__nullex_strncpy" },
  { BUILT_IN_STRCAT,	"__nullex_strcat" },
  { BUILT_IN_STRNCAT,	"__nullex_strncat" },
  { BUILT_IN_STRCMP,	"__nullex_strcmp" },
  { BUILT_IN_STRNCMP,	"__nullex_strncmp" },
  { BUILT_IN_STRLEN,	"__nullex_strlen" },
  { BUILT_IN_STRCHR,	"__nullex_strchr" },
  { BUILT_IN_STRRCHR,	"__nullex_strrchr" },
  { BUILT_IN_STRSTR,	"__nullex_strstr" },
  { BUILT_IN_STRSPN,	"__nullex_strspn" },
  { BUILT_IN_STRCSPN,	"__nullex_strcspn" },
  { BUILT_IN_STRPBRK,	"__nullex_strpbrk" },
  { BUILT_IN_PRINTF,	"__nullex_printf" },
  { BUILT_IN_FPRINTF,	"__nullex_fprintf" },
  { BUILT_IN_SPRINTF,	"__nullex_sprintf" },
  { BUILT_IN_SNPRINTF,	"__nullex_snprintf" },
  { BUILT_IN_SSCANF,	"__nullex_sscanf" }
};

/* Return the checked entry point for FNDECL, or NULL_TREE if it has
   none.  The decl is registered in the symbol table, which both keeps it
   live and means the next lookup finds the same one.  */

static tree
nullex_stub_decl (tree fndecl)
{
  if (!fndecl_built_in_p (fndecl, BUILT_IN_NORMAL))
    return NULL_TREE;

  const char *name = NULL;
  enum built_in_function code = DECL_FUNCTION_CODE (fndecl);
  for (unsigned int i = 0; i < ARRAY_SIZE (nullex_stub_table); ++i)
    if (nullex_stub_table[i].code == code)
      {
	name = nullex_stub_table[i].name;
	break;
      }
  if (name == NULL)
    return NULL_TREE;

  tree id = get_identifier (name);
  if (symtab_node *node = symtab_node::get_for_asmname (id))
    if (TREE_CODE (node->decl) == FUNCTION_DECL)
      return node->decl;

  tree decl = build_decl (DECL_SOURCE_LOCATION (fndecl), FUNCTION_DECL,
			  id, TREE_TYPE (fndecl));
  DECL_ARTIFICIAL (decl) = 1;
  DECL_EXTERNAL (decl) = 1;
  TREE_PUBLIC (decl) = 1;
  TREE_USED (decl) = 1;
  /* The stub throws; that is the whole point of it.  */
  TREE_NOTHROW (decl) = 0;
  SET_DECL_ASSEMBLER_NAME (decl, id);
  cgraph_node::get_create (decl);
  return decl;
}

/* If T is the address of a library function with a checked entry point,
   return the address of that entry point instead, else NULL_TREE.  */

static tree
maybe_redirect_fn_address (tree t)
{
  if (t == NULL_TREE || TREE_CODE (t) != ADDR_EXPR)
    return NULL_TREE;

  tree fn = TREE_OPERAND (t, 0);
  if (TREE_CODE (fn) != FUNCTION_DECL)
    return NULL_TREE;

  tree stub = nullex_stub_decl (fn);
  if (stub == NULL_TREE)
    return NULL_TREE;

  return build_fold_addr_expr (stub);
}

/* Rewrite addresses of library functions taken as values.  The callee of
   a direct call is left alone: that call is checked at the call site, and
   redirecting it would lose the tail call into the real function.  */

static void
redirect_fn_addresses (gimple *stmt)
{
  if (gcall *call = dyn_cast <gcall *> (stmt))
    {
      if (gimple_call_internal_p (call))
	return;
      unsigned int nargs = gimple_call_num_args (call);
      for (unsigned int i = 0; i < nargs; ++i)
	if (tree n = maybe_redirect_fn_address (gimple_call_arg (call, i)))
	  gimple_call_set_arg (call, i, n);
      return;
    }

  /* Every operand position has to be covered, not just the obvious
     assignment: if fp = strlen is redirected but fp == strlen is not, the
     two stop comparing equal, which breaks pointer identity inside the
     ABI rather than fixing anything.  */
  if (gassign *assign = dyn_cast <gassign *> (stmt))
    {
      if (tree n = maybe_redirect_fn_address (gimple_assign_rhs1 (assign)))
	gimple_assign_set_rhs1 (assign, n);
      if (gimple_num_ops (assign) > 2)
	if (tree n = maybe_redirect_fn_address (gimple_assign_rhs2 (assign)))
	  gimple_assign_set_rhs2 (assign, n);
      if (gimple_num_ops (assign) > 3)
	if (tree n = maybe_redirect_fn_address (gimple_assign_rhs3 (assign)))
	  gimple_assign_set_rhs3 (assign, n);
      return;
    }

  if (gcond *cond = dyn_cast <gcond *> (stmt))
    {
      if (tree n = maybe_redirect_fn_address (gimple_cond_lhs (cond)))
	gimple_cond_set_lhs (cond, n);
      if (tree n = maybe_redirect_fn_address (gimple_cond_rhs (cond)))
	gimple_cond_set_rhs (cond, n);
      return;
    }

  if (greturn *ret = dyn_cast <greturn *> (stmt))
    {
      if (tree n = maybe_redirect_fn_address (gimple_return_retval (ret)))
	gimple_return_set_retval (ret, n);
    }
}

/* walk_tree callback for the initializer rewrite below.  */

static tree
nullex_rewrite_addr_r (tree *tp, int *walk_subtrees, void *)
{
  tree t = *tp;

  if (TYPE_P (t))
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }

  if (TREE_CODE (t) == ADDR_EXPR)
    {
      if (tree n = maybe_redirect_fn_address (t))
	*tp = n;
      /* Nothing below an ADDR_EXPR can be another function address.  */
      *walk_subtrees = 0;
    }

  return NULL_TREE;
}

/* Rewrite the addresses of library functions in DECL's initializer, so
   that a pointer formed in static data means what the same expression
   means in a statement.  Without this, whether &strcpy denotes the
   checked entry point would depend on whether the initializer sat at file
   scope or inside a function, which is not a distinction the ABI can
   afford to make.  Rewriting is idempotent: the stub is not itself a
   builtin, so a second pass over the same tree matches nothing.  */

void
nullex_rewrite_initializer (tree decl)
{
  if (!flag_nullptr_exceptions || !flag_exceptions)
    return;

  tree init = DECL_INITIAL (decl);
  if (init == NULL_TREE || init == error_mark_node)
    return;

  walk_tree (&init, nullex_rewrite_addr_r, NULL, NULL);
  DECL_INITIAL (decl) = init;
}

/* Emit, once per translation unit compiled for the ABI, a reference to a
   symbol that only the ABI's own runtime defines.  Linking these objects
   against the ordinary runtime then fails at link time rather than
   producing a program in which one function has two addresses.

   This catches the accident that actually happens -- compiling one way and
   linking the other -- but not a deliberate mix of object files, because
   an ordinary object carries no mark to contradict.  Catching that needs a
   property note and a linker willing to reject a mismatch instead of
   merging it, which is not something the compiler can do alone.

   The marker follows the instrumentation, not the multilib.  A unit built
   for the ABI but with the checks turned off calls nothing in the nullex
   runtime and hands back no stub address, so it has no dependency to
   assert, and asserting one anyway would be wrong: libgcc's crtstuff is
   compiled -fno-exceptions and is linked into every program, so keying the
   marker on the multilib alone made even a pure C link demand the C++
   runtime.

   The reference is one COMDAT pointer for the whole program, and is marked
   preserved so that --gc-sections cannot drop it and with it the check.  */

void
nullex_emit_abi_marker (void)
{
  if (!flag_nullex_abi || !flag_nullptr_exceptions)
    return;

  tree tag_id = get_identifier ("__nullex_abi_v1_link_with_m64nullex");
  tree tag = build_decl (BUILTINS_LOCATION, VAR_DECL, tag_id,
			 char_type_node);
  TREE_PUBLIC (tag) = 1;
  DECL_EXTERNAL (tag) = 1;
  TREE_READONLY (tag) = 1;
  DECL_ARTIFICIAL (tag) = 1;
  SET_DECL_ASSEMBLER_NAME (tag, tag_id);

  tree ref_id = get_identifier ("__nullex_abi_ref");
  tree ref = build_decl (BUILTINS_LOCATION, VAR_DECL, ref_id,
			 build_pointer_type (char_type_node));
  TREE_STATIC (ref) = 1;
  TREE_PUBLIC (ref) = 1;
  TREE_READONLY (ref) = 1;
  TREE_USED (ref) = 1;
  DECL_ARTIFICIAL (ref) = 1;
  DECL_PRESERVE_P (ref) = 1;
  /* "used" keeps the compiler from dropping it; SHF_GNU_RETAIN, which
     comes from the separate "retain" attribute, keeps --gc-sections from
     doing so.  Without the second one the marker is discarded along with
     the undefined reference, and the check disappears silently -- which is
     the worst way for a check like this to fail.  */
  DECL_ATTRIBUTES (ref) = tree_cons (get_identifier ("retain"), NULL_TREE,
				     DECL_ATTRIBUTES (ref));
  DECL_INITIAL (ref) = build_fold_addr_expr (tag);
  SET_DECL_ASSEMBLER_NAME (ref, ref_id);
  make_decl_one_only (ref, ref_id);
  varpool_node::finalize_decl (ref);
}

/* Callback for walk_gimple_seq_mod.  */

static tree
nullptr_exc_stmt (gimple_stmt_iterator *gsi, bool *handled_ops_p,
		  struct walk_stmt_info *)
{
  gimple *stmt = gsi_stmt (*gsi);

  /* Never set this: walk_gimple_stmt only descends into GIMPLE_BIND,
     GIMPLE_TRY and the other sub-sequences when the statement callback
     leaves the operands unhandled.  */
  *handled_ops_p = false;

  if (is_gimple_debug (stmt) || gimple_clobber_p (stmt))
    return NULL_TREE;

  location_t loc = gimple_location (stmt);

  if (gimple_store_p (stmt))
    instrument_deref (gsi, gimple_get_lhs (stmt), loc);
  if (gimple_assign_single_p (stmt))
    instrument_deref (gsi, gimple_assign_rhs1 (stmt), loc);
  if (is_gimple_call (stmt) && !gimple_call_internal_p (stmt))
    {
      unsigned int nargs = gimple_call_num_args (stmt);
      for (unsigned int i = 0; i < nargs; ++i)
	{
	  tree arg = gimple_call_arg (stmt, i);
	  if (is_gimple_reg (arg) || is_gimple_min_invariant (arg))
	    continue;
	  instrument_deref (gsi, arg, loc);
	}
      instrument_nonnull_args (gsi, loc);
    }

  if (is_gimple_assign (stmt)
      && gimple_assign_rhs_code (stmt) == POINTER_PLUS_EXPR)
    instrument_pointer_plus (gsi, gimple_assign_rhs1 (stmt),
			     gimple_assign_rhs2 (stmt), loc);

  redirect_fn_addresses (stmt);

  return NULL_TREE;
}

namespace {

const pass_data pass_data_nullptr_exceptions =
{
  GIMPLE_PASS, /* type */
  "nullptr_exc", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_nullptr_exceptions : public gimple_opt_pass
{
public:
  pass_nullptr_exceptions (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_nullptr_exceptions, ctxt)
  {}

  /* opt_pass methods: */
  bool gate (function *fun) final override
  {
    return (opt_for_fn (fun->decl, flag_nullptr_exceptions)
	    && opt_for_fn (fun->decl, flag_exceptions));
  }

  unsigned int execute (function *) final override;

}; // class pass_nullptr_exceptions

unsigned int
pass_nullptr_exceptions::execute (function *)
{
  gimple_seq body = gimple_body (current_function_decl);
  struct walk_stmt_info wi;

  memset (&wi, 0, sizeof (wi));
  walk_gimple_seq_mod (&body, nullptr_exc_stmt, NULL, &wi);
  gimple_set_body (current_function_decl, body);

  return 0;
}

} // anon namespace

gimple_opt_pass *
make_pass_nullptr_exceptions (gcc::context *ctxt)
{
  return new pass_nullptr_exceptions (ctxt);
}
