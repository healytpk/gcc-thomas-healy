/* typedefangles.cpp -- typedef<> / typedef<tmpl> field-mapping extension.
   Copyright (C) 2024 Free Software Foundation, Inc.

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
   <http://www.gnu.org/licenses/>.

   ── Overview ──────────────────────────────────────────────────────────────

   Implements two related syntactic forms:

     typedef<>          S T;    -- "newtype": a fresh struct with the same
                                   field names and types as S, but a distinct
                                   C++ type (strong typedef).

     typedef< tmpl >    S T;    -- "field map": a fresh struct where every
                                   field type ft of S is replaced by tmpl<ft>.

   Inherited non-static data members are included and flattened into T.
   Static members, artificial fields (vptr, padding), and member functions
   are excluded.  Virtual base classes are visited at most once.
   Nested struct-typed fields are wrapped as a whole (not recursed into).  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "cp-tree.h"
#include "parser.h"
#include "typedefangles.h"

/* ── collect_mapped_fields ────────────────────────────────────────────────

   Recursively collect every non-static, non-artificial FIELD_DECL from
   TYPE (direct and inherited), walking base classes depth-first so that
   fields appear in C++ layout order.  VISITED de-duplicates virtual bases.
   Results are appended to OUT_FIELDS.  */

static void
collect_mapped_fields (tree type, hash_set<tree> *visited,
		       auto_vec<tree> *out_fields)
{
  type = complete_type (TYPE_MAIN_VARIANT (type));

  if (!type || type == error_mark_node
      || TREE_CODE (type) != RECORD_TYPE)
    return;

  /* Mark as visited; if already present this is a repeated virtual base.  */
  if (visited->add (type))
    return;

  /* Recurse into base classes before adding direct fields so the layout
     order (base fields first) is preserved in OUT_FIELDS.  */
  tree binfo = TYPE_BINFO (type);
  if (binfo)
    {
      int n = BINFO_N_BASE_BINFOS (binfo);
      for (int i = 0; i < n; i++)
	collect_mapped_fields (BINFO_TYPE (BINFO_BASE_BINFO (binfo, i)),
			       visited, out_fields);
    }

  /* Append direct non-static data members.

     FIELD_DECL  = non-static data member  (static members are VAR_DECL).
     DECL_ARTIFICIAL filters out compiler-injected fields such as the
     virtual-function-table pointer and anonymous-struct padding.  */
  for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
    if (TREE_CODE (f) == FIELD_DECL && !DECL_ARTIFICIAL (f))
      out_fields->safe_push (f);
}

/* ── perform_typedef_field_map ────────────────────────────────────────────

   Called by cp_parser_typedef_field_map (parser.cc) after the token stream
   has been parsed.

   TMPL        : TEMPLATE_DECL to instantiate for each field, or NULL_TREE
                 for the identity / newtype case (typedef<>).
   SOURCE_TYPE : the struct or class to map over.
   NEW_NAME    : IDENTIFIER_NODE for the synthesised type.
   LOC         : source location of the typedef<> construct.  */

void
perform_typedef_field_map (tree tmpl, tree source_type,
			   tree new_name, location_t loc)
{
  /* ── 1. Validate source type ──────────────────────────────────────────── */

  source_type = complete_type (source_type);
  if (!source_type || source_type == error_mark_node)
    return;

  if (TREE_CODE (source_type) != RECORD_TYPE)
    {
      error_at (loc,
		"%<typedef<>%>: source %qT is not a struct or class",
		source_type);
      return;
    }

  /* ── 2. Validate template arity (non-identity case) ──────────────────── */

  if (tmpl != NULL_TREE)
    {
      gcc_assert (TREE_CODE (tmpl) == TEMPLATE_DECL);
      if (DECL_NTPARMS (tmpl) < 1)
	{
	  error_at (loc,
		    "%<typedef<>%>: %qD takes no type parameters; "
		    "expected a template taking at least one", tmpl);
	  return;
	}
    }

  /* ── 3. Collect all fields in layout order ────────────────────────────── */

  auto_vec<tree> all_fields;
  hash_set<tree> visited;
  collect_mapped_fields (source_type, &visited, &all_fields);

  /* ── 4. Open the new struct ───────────────────────────────────────────── */

  /* xref_tag registers NEW_NAME in the current scope via pushtag;
     TAG_how::CURRENT_ONLY means we are defining it here and now.  */
  tree new_record = xref_tag (record_type, new_name,
			      TAG_how::CURRENT_ONLY,
			      /*template_header_p=*/false);
  if (new_record == error_mark_node)
    return;

  new_record = begin_class_definition (new_record);
  if (new_record == error_mark_node)
    return;

  /* Initialise BINFO even though we have no base classes.
     xref_basetypes must always be called after begin_class_definition;
     without it TYPE_BINFO (new_record) remains NULL and finish_struct_1
     crashes in check_bases when it dereferences it.  */
  xref_basetypes (new_record, /*base_list=*/NULL_TREE);

  /* ── 5. Map each field ────────────────────────────────────────────────── */

  for (tree field : all_fields)
    {
      if (DECL_NAME (field) == NULL_TREE)
        {
          error_at (DECL_SOURCE_LOCATION (field),
                    "%<typedef<>%>: anonymous members are not supported");

          finish_struct (new_record, /*attributes=*/NULL_TREE);
          return;
        }

      tree field_type = TREE_TYPE (field);
      tree mapped_type;

      if (tmpl == NULL_TREE)
	{
	  /* typedef<> identity transform: keep the field type as-is.  */
	  mapped_type = field_type;
	}
      else
	{
	  /* typedef<tmpl>: instantiate tmpl<field_type>.  */
	  tree targs = make_tree_vec (1);
	  TREE_VEC_ELT (targs, 0) = field_type;
	  mapped_type = lookup_template_class (tmpl, targs,
					       /*in_decl=*/NULL_TREE,
					       /*context=*/NULL_TREE,
					       tf_error | tf_user);
	  if (mapped_type == error_mark_node)
	    {
	      /* lookup_template_class already emitted the instantiation
		 error; add a note pointing at the offending field.  */
	      inform (DECL_SOURCE_LOCATION (field),
		      "  while mapping field %qD of type %qT",
		      field, field_type);
	      /* Must still call finish_struct to keep GCC state consistent. */
	      finish_struct (new_record, /*attributes=*/NULL_TREE);
	      return;
	    }

	  /* Force full instantiation of the template class.  lookup_template_class
	     may return an incomplete type; without complete_type, TYPE_BINFO is
	     null and is_really_empty_class crashes when the variable is later declared.  */
	  mapped_type = complete_type (mapped_type);
	  if (!COMPLETE_TYPE_P (mapped_type))
	    {
	      error_at (loc, "%<typedef<>%>: could not instantiate %qT for "
	                "field %qD", mapped_type, field);
	      finish_struct (new_record, /*attributes=*/NULL_TREE);
	      return;
	    }
	}

      tree new_field = build_decl (DECL_SOURCE_LOCATION (field),
				   FIELD_DECL,
				   DECL_NAME (field),
				   mapped_type);
      DECL_CONTEXT (new_field) = new_record;
      finish_member_declaration (new_field);
    }

  /* ── 6. Finalise ─────────────────────────────────────────────────────── */

  finish_struct (new_record, /*attributes=*/NULL_TREE);
}

#if 0
tree
perform_datasizeof (tree type, location_t loc)
{
  type = complete_type (type);
  if (!type || type == error_mark_node)
    return error_mark_node;

  /* Non-class types carry no trailing padding — datasizeof == sizeof.  */
  if (!CLASS_TYPE_P (type))
    return TYPE_SIZE_UNIT (type);

  if (!COMPLETE_TYPE_P (type))
    {
      error_at (loc, "%<__datasizeof%> of incomplete type %qT", type);
      return error_mark_node;
    }

  unsigned HOST_WIDE_INT max_end = 0;  /* in bits */

  for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
    {
      if (TREE_CODE (f) != FIELD_DECL || DECL_ARTIFICIAL (f))
        continue;
      unsigned HOST_WIDE_INT end =
        tree_to_uhwi (bit_position (f)) + tree_to_uhwi (DECL_SIZE (f));
      if (end > max_end)
        max_end = end;
    }

  /* Round up to full bytes.  */
  return build_int_cst (size_type_node,
                        (max_end + BITS_PER_UNIT - 1) / BITS_PER_UNIT);
}
#endif

