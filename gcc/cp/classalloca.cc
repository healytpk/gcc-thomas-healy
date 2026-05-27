#define INCLUDE_STRING
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "cp-tree.h"
#include "stringpool.h"
#include "cgraph.h"
#include "stor-layout.h"
#include "builtins.h"
#include "function.h"
#include "tree-iterator.h"
#include "parser.h"
#include "gimplify.h"
#include "classalloca.h"

/* ------------------------------------------------------------------ *
 * FrameFooter type (synthesised once)
 *
 *   struct FrameFooter {
 *     void*          obj;
 *     void         (*destructor)(void*);
 *     FrameFooter*   prev;
 *   };
 * ------------------------------------------------------------------ */

static tree footer_type;

static tree
get_frame_footer_type (void)
{
  if (footer_type)
    return footer_type;

  footer_type = make_node (RECORD_TYPE);

  tree obj_field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
                               get_identifier ("obj"),
                               ptr_type_node);
  DECL_CONTEXT (obj_field) = footer_type;

  tree dtor_fn_type = build_function_type_list (void_type_node,
                                                ptr_type_node,
                                                NULL_TREE);
  tree dtor_field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
                                get_identifier ("destructor"),
                                build_pointer_type (dtor_fn_type));
  DECL_CONTEXT (dtor_field) = footer_type;

  tree prev_field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
                                get_identifier ("prev"),
                                build_pointer_type (footer_type));
  DECL_CONTEXT (prev_field) = footer_type;

  DECL_CHAIN (obj_field)  = dtor_field;
  DECL_CHAIN (dtor_field) = prev_field;
  TYPE_FIELDS (footer_type) = obj_field;

  layout_type (footer_type);
  return footer_type;
}

/* ------------------------------------------------------------------ *
 * Frame<T> type (synthesised per type)
 *
 *   struct alignas(T) Frame {
 *     unsigned char buf[sizeof(T)];
 *     FrameFooter   hdr;
 *   };
 * ------------------------------------------------------------------ */

static tree
get_frame_type (tree type)
{
  tree frame_type = make_node (RECORD_TYPE);

  tree buf_type = build_array_type_nelts (unsigned_char_type_node,
                                          tree_to_uhwi (TYPE_SIZE_UNIT (type)));
  tree buf_field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
                               get_identifier ("buf"),
                               buf_type);
  DECL_CONTEXT (buf_field) = frame_type;

  tree hdr_field = build_decl (BUILTINS_LOCATION, FIELD_DECL,
                               get_identifier ("hdr"),
                               get_frame_footer_type ());
  DECL_CONTEXT (hdr_field) = frame_type;

  DECL_CHAIN (buf_field) = hdr_field;
  TYPE_FIELDS (frame_type) = buf_field;

  /* alignas(T) on the Frame struct itself ensures buf at offset zero
     inherits the correct alignment without any per-field annotation.  */
  SET_TYPE_ALIGN (frame_type, TYPE_ALIGN (type));
  TYPE_USER_ALIGN (frame_type) = 1;

  layout_type (frame_type);
  return frame_type;
}

/* ------------------------------------------------------------------ *
 * destroy_adapter<T>
 *
 *   static void __classalloca_destroy (void* p)
 *   {
 *     static_cast<T*>(p)->~T();
 *   }
 * ------------------------------------------------------------------ */

static tree
build_classalloca_dtor_pointer (tree type, tree target_ptr_type)
{
  type = TYPE_MAIN_VARIANT (type);

  if (!CLASS_TYPE_P (type))
    return error_mark_node;

  /* No destructor needed. Your unwind/destruction code must skip null. */
  if (!TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type))
    return build_zero_cst (target_ptr_type);

  tree dtor = get_dtor (type, tf_warning_or_error);
  if (!dtor || dtor == error_mark_node)
    return error_mark_node;

  gcc_assert (TREE_CODE (dtor) == FUNCTION_DECL);

  /* Make sure the destructor is considered used/emittable. */
  if (!mark_used (dtor, tf_warning_or_error))
    return error_mark_node;

  tree addr = build_address (dtor);
  if (addr == error_mark_node)
    return error_mark_node;

  /* Deliberate type erasure into the exact Frame field type. */
  return fold_convert (target_ptr_type, addr);
}

/* ------------------------------------------------------------------ *
 * Chain head
 *
 *   FrameFooter* .classalloca_chain = nullptr;
 * ------------------------------------------------------------------ */
 
static tree
classalloca_chain_head (void)
{
  if (cfun->classalloca_chain_head_decl)
    return cfun->classalloca_chain_head_decl;

  tree head_type = build_pointer_type (get_frame_footer_type ());
  tree decl = build_decl (input_location, VAR_DECL,
                          get_identifier (".classalloca_chain"),
                          head_type);
  DECL_ARTIFICIAL (decl) = 1;
  DECL_IGNORED_P (decl)  = 1;
  DECL_CONTEXT (decl)    = current_function_decl;
  layout_decl (decl, 0);

  /* Add directly to the function's locals rather than to the current
     C++ binding level.  Initialisation is prepended to the function
     body in finish_function, ensuring the variable is always
     zero-initialised regardless of where classalloca first appears.  */
  gimple_add_tmp_var_fn (cfun, decl);

  cfun->calls_classalloca = 1;
  cfun->calls_alloca      = 1;
  cfun->classalloca_chain_head_decl = decl;

  return decl;
}

/* ------------------------------------------------------------------ *
 * Internal: build_classalloca_construct_from_value.
 *
 * Allocate a frame for TYPE, initialise the object storage from VALUE,
 * and yield an lvalue referring to the constructed object.
 * ------------------------------------------------------------------ */

static tree
build_classalloca_construct_from_value (tree type, tree value)
{
  tree frame_type      = get_frame_type (type);
  tree frame_ptr_type  = build_pointer_type (frame_type);
  tree footer_ptr_type = build_pointer_type (get_frame_footer_type ());

  tree buf_field = TYPE_FIELDS (frame_type);
  tree hdr_field = DECL_CHAIN (buf_field);

  tree obj_field  = TYPE_FIELDS (get_frame_footer_type ());
  tree dtor_field = DECL_CHAIN (obj_field);
  tree prev_field = DECL_CHAIN (dtor_field);

  /* previous_head = head  */
  tree chain_head = classalloca_chain_head ();
  tree prev_head  = save_expr (chain_head);

  /* f = (Frame<T>*) __builtin_alloca_with_align(  sizeof(Frame<T>), alignof(T)  )  */
  tree alloca_fn = builtin_decl_explicit (BUILT_IN_ALLOCA_WITH_ALIGN);

  tree alloca_call = build_call_expr (alloca_fn, 2,
                                      TYPE_SIZE_UNIT (frame_type),
                                      build_int_cst (integer_type_node,
                                                     TYPE_ALIGN (type)));

  // Use fold_convert instead of build_nop. It correctly builds a
  // CONVERT_EXPR that satisfies C++ strict type checking.
  tree f = save_expr (fold_convert (frame_ptr_type, alloca_call));

  tree f_deref = build1 (INDIRECT_REF, frame_type, f);
  tree hdr_ref = build3 (COMPONENT_REF, get_frame_footer_type (),
                         f_deref, hdr_field, NULL_TREE);

#define HDR_FIELD(fld) \
  build3 (COMPONENT_REF, TREE_TYPE (fld), hdr_ref, (fld), NULL_TREE)

  /* buf_ptr = (T*) &f->buf  */
  tree buf_ref = build3 (COMPONENT_REF, TREE_TYPE (buf_field),
                         f_deref, buf_field, NULL_TREE);
  tree buf_ptr = fold_convert (build_pointer_type (type),
                               build_fold_addr_expr (buf_ref));

  /* f->hdr.obj = (void*) buf_ptr  */
  tree write_obj
    = build2 (MODIFY_EXPR, ptr_type_node,
              HDR_FIELD (obj_field),
              build_nop (ptr_type_node, buf_ptr));

  /* f->hdr.destructor = erased address of T::~T  */
  tree dtor_ptr = build_classalloca_dtor_pointer (type, TREE_TYPE (dtor_field));
  if (dtor_ptr == error_mark_node)
    return error_mark_node;

  tree write_dtor
    = build2 (MODIFY_EXPR, TREE_TYPE (dtor_field),
              HDR_FIELD (dtor_field),
              dtor_ptr);

  /* prev = previous head */
  tree write_prev
    = build2 (MODIFY_EXPR, TREE_TYPE (HDR_FIELD (prev_field)),
              HDR_FIELD (prev_field),
              build_nop (TREE_TYPE (HDR_FIELD (prev_field)), prev_head));

  // 1. Dereference the pointer to get the actual object instance
  tree instance = cp_build_fold_indirect_ref (buf_ptr);

  /* Initialise the destination object from VALUE.

     Do not lower this to T::T(value).  That would require a copy/move
     constructor for same-type prvalues, which defeats the point of allowing
     classalloca(std::mutex{}) and other immovable prvalue expressions.  */
  tree init = cp_build_modify_expr (input_location,
                                    instance,
                                    INIT_EXPR,
                                    value,
                                    tf_warning_or_error);
  if (init == error_mark_node)
    return error_mark_node;

  /* FIX: Explicitly cast &f->hdr to match the global chain_head's type */
  tree update_head
    = build2 (MODIFY_EXPR, TREE_TYPE (chain_head),
              chain_head,
              build_nop (TREE_TYPE (chain_head), build1 (ADDR_EXPR, footer_ptr_type, hdr_ref)));

#undef HDR_FIELD

  tree retval = build2 (COMPOUND_EXPR, type,
                 build2 (COMPOUND_EXPR, void_type_node,
                         build2 (COMPOUND_EXPR, void_type_node,
                                 build2 (COMPOUND_EXPR, void_type_node,
                                         build2 (COMPOUND_EXPR, void_type_node,
                                                 build2 (COMPOUND_EXPR, void_type_node,
                                                         build2 (COMPOUND_EXPR, void_type_node,
                                                                 prev_head, f),
                                                         write_obj),
                                                 write_dtor),
                                         write_prev),
                                 init),
                         update_head),
                 instance);

  return retval;
}

/* ------------------------------------------------------------------ *
 * Public: build_classalloca_from_value
 *
 *   classalloca(expr)
 *
 * The operand must be a class-type prvalue.  The result is a pointer to the
 * object created in the classalloca frame.
 * ------------------------------------------------------------------ */

tree
build_classalloca_from_value (tree value)
{
  if (value == error_mark_node)
    return error_mark_node;

  tree type = TREE_TYPE (value);
  if (!type || type == error_mark_node)
    return error_mark_node;

  if (TYPE_REF_P (type))
    type = TREE_TYPE (type);

  if (VOID_TYPE_P (type))
    {
      error ("classalloca operand must not have type %<void%>");
      return error_mark_node;
    }

  type = TYPE_MAIN_VARIANT (type);

  if (!CLASS_TYPE_P (type))
    {
      error ("classalloca operand must have class type");
      return error_mark_node;
    }

  if (TYPE_SIZE_UNIT (type) == NULL_TREE)
    {
      error ("classalloca operand has incomplete type");
      return error_mark_node;
    }

  /* classalloca is deliberately PRvalue-only.  This rejects:

       T t;
       classalloca(t);

     Users should write classalloca(T(t)) if they really want to copy.  */
  if (glvalue_p (value))
    {
      error ("classalloca operand must be a prvalue");
      return error_mark_node;
    }

  /* If we are in an unevaluated context, do not generate runtime logic.
     Just yield an lvalue of the correct type.

     This makes:

       decltype(classalloca(T{}))

     behave as T&, matching the runtime expression category.  */
  if (cp_unevaluated_operand)
    return cp_build_fold_indirect_ref
      (build_zero_cst (build_pointer_type (type)));

  return build_classalloca_construct_from_value (type, value);
}

/* ------------------------------------------------------------------ *
 * Public: build_classalloca_cleanup_loop
 * Generates the while-loop to walk the chain and call destructors.
 * ------------------------------------------------------------------ */
tree
build_classalloca_cleanup_loop (void)
{
  tree head_decl = cfun->classalloca_chain_head_decl;
  tree ftr_type  = get_frame_footer_type ();

  gcc_assert (head_decl != NULL_TREE);

  tree footer_ptr_type = TREE_TYPE (head_decl);

  /* while (.classalloca_chain != nullptr) */
  tree cond = build2 (NE_EXPR,
                      boolean_type_node,
                      head_decl,
                      build_zero_cst (footer_ptr_type));

  tree obj_field = TYPE_FIELDS (ftr_type);
  tree dtor_field = DECL_CHAIN (obj_field);
  tree prev_field = DECL_CHAIN (dtor_field);

  tree head_deref = build1 (INDIRECT_REF, ftr_type, head_decl);

  tree obj_ptr = build3 (COMPONENT_REF,
                         TREE_TYPE (obj_field),
                         head_deref,
                         obj_field,
                         NULL_TREE);

  tree dtor_ptr = build3 (COMPONENT_REF,
                          TREE_TYPE (dtor_field),
                          head_deref,
                          dtor_field,
                          NULL_TREE);

  tree prev_ptr = build3 (COMPONENT_REF,
                          TREE_TYPE (prev_field),
                          head_deref,
                          prev_field,
                          NULL_TREE);

  /*
     if (dtor_ptr != nullptr)
       dtor_ptr(obj_ptr);
  */
  tree dtor_nonnull = build2 (NE_EXPR,
                              boolean_type_node,
                              dtor_ptr,
                              build_zero_cst (TREE_TYPE (dtor_ptr)));

  tree call_dtor = build_call_nary (void_type_node,
                                    dtor_ptr,
                                    1,
                                    obj_ptr);

  tree guarded_call = build3 (COND_EXPR,
                              void_type_node,
                              dtor_nonnull,
                              call_dtor,
                              void_node);

  /* .classalloca_chain = prev_ptr; */
  tree advance = build2 (MODIFY_EXPR,
                         TREE_TYPE (head_decl),
                         head_decl,
                         prev_ptr);

  tree loop_body = alloc_stmt_list ();

  append_to_statement_list (
    build_stmt (input_location, EXPR_STMT, guarded_call),
    &loop_body);

  append_to_statement_list (
    build_stmt (input_location, EXPR_STMT, advance),
    &loop_body);

  return build_stmt (input_location, WHILE_STMT,
                     cond,
                     loop_body,
                     NULL_TREE,
                     NULL_TREE,
                     NULL_TREE);
}
