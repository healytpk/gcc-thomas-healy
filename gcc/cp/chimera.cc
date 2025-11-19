#include "chimera.h"
#include "stringpool.h"

/* Return true if TYPE is an instantiation of std::tuple<...>.
   If ARGS_OUT is non-null, store TYPE_TI_ARGS(TYPE) into *ARGS_OUT.  */

bool
std_tuple_type_p (tree type, tree *args_out)
{
  if (!type)
    return false;

  type = TYPE_MAIN_VARIANT (type);

  if (!MAYBE_CLASS_TYPE_P (type))
    return false;

  if (dependent_type_p (type) || uses_template_parms (type))
    return false;

  tree tinfo = TYPE_TEMPLATE_INFO (type);
  if (!tinfo)
    return false;

  tree tmpl = TI_TEMPLATE (tinfo);
  if (!tmpl || TREE_CODE (tmpl) != TEMPLATE_DECL)
    return false;

  if (!DECL_NAMESPACE_STD_P (CP_DECL_CONTEXT (tmpl)))
    return false;

  tree name = DECL_NAME (tmpl);
  if (!name || name != get_identifier ("tuple"))
    return false;

  if (args_out)
    *args_out = TYPE_TI_ARGS (type);

  return true;
}

tree
check_for_chimera(cp_expr object, tree name, bool template_p, tsubst_flags_t complain)
{
  /* Chimeric pointer extension:
  If lookup in std::chimeric_ptr<Bases...> itself fails,
  try the multi-base chimeric lookup before giving up.

     p->name

  is then handled as though it were:

     static_cast<ChosenBase*>(p)->name

  where ChosenBase is the first base in the template argument
  pack that declares 'name'.  */

  if ( (TREE_CODE(name) != IDENTIFIER_NODE) && (TREE_CODE(name) != TEMPLATE_ID_EXPR) ) return NULL_TREE;

  if ( false == cp_is_chimera( TREE_TYPE( object.get_value() ) ) ) return NULL_TREE;

  return build_chimeric_member_access(
    EXPR_LOCATION( object.get_value() ),
    object,
    name,
    template_p,
    /*preserve_reference=*/ false,
    complain);
}

tree
chimera_get_nested_typedef_type (tree chim_type, const char* type_name)
{
  if (!chim_type)
    return NULL_TREE;

  chim_type = non_reference (chim_type);
  chim_type = TYPE_MAIN_VARIANT (chim_type);

  if (!MAYBE_CLASS_TYPE_P (chim_type))
    return NULL_TREE;

  /* Only handle non-dependent, already-instantiated types.  */
  if (dependent_type_p (chim_type) || uses_template_parms (chim_type))
    return NULL_TREE;

  tree id = get_identifier(type_name);

  access_failure_info afi;
  tree mem = lookup_member(chim_type, id,
                           /*protect=*/0,
                           /*want_type=*/true,
                           /*complain=*/tf_none,
                           &afi);

  if (!mem)
    return NULL_TREE;

  /* lookup_member can return various wrappers; handle the common ones. */
  if (TREE_CODE(mem) == TREE_LIST)
    mem = TREE_VALUE(mem);

  if (!mem || TREE_CODE(mem) != TYPE_DECL)
    return NULL_TREE;

  return TREE_TYPE(mem); /* The aliased type */
}

/* Extract a TREE_VEC of types from a nested alias std::tuple<...> named NAME_OF_TUPLE.
   Returns NULL_TREE on failure.  */

tree
chimera_vec (tree chim_type, char const *const name_of_tuple)
{
  chim_type = non_reference(chim_type);
  chim_type = TYPE_MAIN_VARIANT(chim_type);

  if (!COMPLETE_TYPE_P(chim_type))
    return NULL_TREE;

  /* Get the nested alias type: std::tuple<I1, I2, ...> */
  tree tup = chimera_get_nested_typedef_type(chim_type, name_of_tuple);
  if (!tup)
    return NULL_TREE;

  tree tuple_args = NULL_TREE;
  if (!std_tuple_type_p(tup, &tuple_args))
    return NULL_TREE;

  /* Expand TYPE_TI_ARGS(std::tuple<...>) into a TREE_VEC of the element types. */
  if (!tuple_args || TREE_CODE(tuple_args) != TREE_VEC)
    return NULL_TREE;

  tree vec = tuple_args;
  if (TREE_VEC_LENGTH(vec) == 1 && ARGUMENT_PACK_P(TREE_VEC_ELT(vec, 0)))
    {
      tree pack_args = ARGUMENT_PACK_ARGS(TREE_VEC_ELT(vec, 0));
      if (!pack_args || TREE_CODE(pack_args) != TREE_VEC)
        return NULL_TREE;
      vec = pack_args;
    }

  /* Here vec already holds interface *types* (not pointers). */
  return vec;
}

/* True if TYPE declares a nested type named __tag_chimera.  */
bool
chimera_tag_type_p (tree type)
{
  return NULL_TREE != chimera_get_nested_typedef_type (type, "__tag_chimera");
}

bool
chimera_type_p (tree type, tree *bases_pack)
{
  if (!chimera_tag_type_p (type))
    return false;

  if (bases_pack)
    {
      tree ifaces = chimera_vec (type, "ChimericInterfacesTuple_t");
      if (!ifaces || TREE_CODE (ifaces) != TREE_VEC)
        return false;
      *bases_pack = ifaces;
    }

  return true;
}

bool
chimera_has_option_named_p (tree chim_type, const char* opt_name)
{
  tree opts = chimera_vec (chim_type, "ChimericOptionsTuple_t");
  if (!opts || TREE_CODE (opts) != TREE_VEC)
    return false;

  tree want = get_identifier (opt_name);

  for (int i = 0; i < TREE_VEC_LENGTH (opts); ++i)
    {
      tree t = TREE_VEC_ELT (opts, i);
      if (!TYPE_P (t))
        continue;

      t = TYPE_MAIN_VARIANT (t);

      tree td = TYPE_NAME (t);
      if (!td || TREE_CODE (td) != TYPE_DECL)
        continue;

      if (DECL_NAME (td) != want)
        continue;

      return true;
    }

  return false;
}

bool
cp_is_chimera (tree type)
{
  return chimera_type_p (type, (tree *) NULL);
}

/* Perform the chimeric-pointer member lookup:

     std::chimeric_ptr<B1, B2, ...> p;
     p->member;

   Search for 'member' in B1, then B2, etc.  The first base that yields a
   viable lookup wins. Conceptually we then bind the expression as though

     static_cast<ChosenBase*>(p)->member

   had been written, where ChosenBase is the first base in the template
   argument pack that declares 'member'.

   OBJECT is the original object expression (p) as a cp_expr.
   NAME is the unqualified member name identifier.
   PRESERVE_REFERENCE and COMPLAIN are as for build_class_member_access_expr.

   Returns error_mark_node if TYPE(OBJECT) is not std::chimeric_ptr
   or if no base declares NAME. */

tree
build_chimeric_member_access (location_t loc,
			      cp_expr object,
			      tree name,
			      bool template_p,
			      bool preserve_reference,
			      tsubst_flags_t complain)
{
  (void) preserve_reference;
  tree obj = object.get_value ();
  tree obj_type = TREE_TYPE (obj);

  /* Extract the unqualified identifier used for lookup (e.g. 'set_text').  */
  tree member_id = NULL_TREE;

  if (TREE_CODE (name) == IDENTIFIER_NODE)
    member_id = name;
  else if (DECL_P (name))
    member_id = DECL_NAME (name);
  else if (TREE_CODE (name) == TEMPLATE_ID_EXPR)
    {
      tree tmpl = TREE_OPERAND (name, 0);
      if (DECL_P (tmpl))
	member_id = DECL_NAME (tmpl);
    }

  if (!member_id || TREE_CODE (member_id) != IDENTIFIER_NODE)
    return NULL_TREE;

  /* Don't apply chimeric semantics inside std::chimeric_ptr's own
     member functions/constructors, where 'object' is the current class.  */
  if (current_class_type
      && same_type_ignoring_top_level_qualifiers_p (TYPE_MAIN_VARIANT (obj_type),
                                                    TYPE_MAIN_VARIANT (current_class_type)))
    return NULL_TREE;

  tree bases_pack = NULL_TREE;

  if (!chimera_type_p (obj_type, &bases_pack))
    return NULL_TREE; /* Not a chimeric pointer; caller will fall back. */

  if (!bases_pack || TREE_CODE (bases_pack) != TREE_VEC)
    return NULL_TREE;

  int const tree_len = TREE_VEC_LENGTH (bases_pack);
  if (flag_chimera_debug) inform (loc,
        "DEBUG chimeric: %<bases_pack%> code=%s length=%d",
        get_tree_code_name (TREE_CODE (bases_pack)),
        tree_len);

  tree chosen_base = NULL_TREE;
  tree first_match_base = NULL_TREE;
  bool const forbid_amb = chimera_has_option_named_p (obj_type, "forbid_ambiguity");

  for (int i = 0; i < tree_len; ++i)
    {
      tree base_type = TREE_VEC_ELT (bases_pack, i);
      if (!TYPE_P (base_type))
	      continue;

      if (flag_chimera_debug) inform (loc,
            "DEBUG chimeric: %<base_type%>[%d]%<=%>%qT %<COMPLETE_TYPE_P%>=%d",
            i, base_type, (int)COMPLETE_TYPE_P (base_type));

      /* Be conservative around incomplete types: if the base is not
         complete yet, just skip it for the chimeric lookup and let the
         normal diagnostics handle any ill-formed code.  */
      if (!COMPLETE_TYPE_P (base_type))
        continue;

      if (flag_chimera_debug) inform (loc,
              "DEBUG chimeric lookup: base=%qT member=%qE protect=%d %<want_type%>=%d complain=0x%x complete=%d",
              base_type,
              member_id,
              0,
              0,
              (int)(complain & ~tf_error),
              (int)COMPLETE_TYPE_P (base_type));

      access_failure_info afi;
      tree member = lookup_member (base_type, member_id,
                                   /*protect=*/0,
                                   /*want_type=*/false,
                                   complain & ~tf_error,
                                   &afi);
      if (!member)
        {
          if (flag_chimera_debug) inform (loc,
                  "DEBUG chimeric lookup: %<lookup_member%> returned NULL for base=%qT member=%qE",
                  base_type, member_id);
          continue;
        }

      if (flag_chimera_debug) inform (loc,
              "DEBUG chimeric lookup: %<lookup_member%> FOUND %qD of type %qT",
              member, TREE_TYPE (member));

      if (!chosen_base)
        {
          chosen_base = base_type;
          first_match_base = base_type;

          /* Default behavior: first match wins.  */
          if (!forbid_amb)
            break;

          /* forbid_ambiguity: keep searching for a second match.  */
        }
      else if (forbid_amb)
        {
          if (complain & tf_error)
            error_at (loc,
                      "ambiguous member %qE for %<std::chimeric_ptr%>: found in both %qT and %qT",
                      member_id, first_match_base, base_type);
          return error_mark_node;
        }
    }

  if (!chosen_base)
    {
      if (complain & tf_error)
	error_at (loc,
		  "no member %qE found in any base of %<std::chimeric_ptr%>",
		  member_id);
      return error_mark_node;
    }

  /* Cast the chimeric_ptr to a pointer to the chosen base, relying on
     its conversion operators:
       static_cast<ChosenBase*>(p)
   */

  tree base_ptr_type = build_pointer_type (chosen_base);

  if (flag_chimera_debug) inform (loc,
	  "DEBUG chimeric: casting %qT to %qT",
	  obj_type, base_ptr_type);

  /* Use a C-style cast builder here; it will select the appropriate
     user-defined conversion (operator B*()) if available.  */
  tree base_ptr_expr
    = cp_build_c_cast (loc,          /* location_t */
		       base_ptr_type,/* target type */
		       obj,          /* source expression */
		       complain & ~tf_error); /* tsubst_flags_t */

  if (base_ptr_expr == error_mark_node)
    {
      if (complain & tf_error)
	error_at (loc,
		  "%<std::chimeric_ptr%>: cannot convert to selected base pointer %qT",
		  chosen_base);
      return error_mark_node;
    }

  /* Now build (*base_ptr_expr).name via the usual machinery.  */
  tree deref
    = cp_build_indirect_ref (loc, base_ptr_expr, RO_ARROW, complain);
  if (deref == error_mark_node)
    return error_mark_node;

  cp_expr deref_cp (deref);

  /* NAME here is still the identifier (or template-id) the user wrote.
     TREE_TYPE(deref_cp) is the chosen base (Colorful/Text/…), so this
     does not re-enter the chimeric logic.  */
  return finish_class_member_access_expr (deref_cp, name, template_p, complain);
}
