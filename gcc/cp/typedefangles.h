/* typedefangles.h -- typedef<> / typedef<tmpl> field-mapping extension.
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
   <http://www.gnu.org/licenses/>.  */

#ifndef GCC_CP_TYPEDEFANGLES_H
#define GCC_CP_TYPEDEFANGLES_H

/* Must be included after cp-tree.h.

   Semantic transformation called by cp_parser_typedef_field_map (parser.cc)
   after the token stream has been parsed.  */

extern void perform_typedef_field_map (tree tmpl, tree source_type,
				       tree new_name, location_t loc);

#endif /* GCC_CP_TYPEDEFANGLES_HPP */
