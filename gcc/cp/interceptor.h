/* Interceptor functions.
   Copyright (C) 2013-2026 Free Software Foundation, Inc.

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

#ifndef GCC_INTERCEPTOR_H
#define GCC_INTERCEPTOR_H

// The next 3 lines are for avoiding including another header file
// to define the type 'location_t'.
template<bool b> struct interceptor_helper        { typedef long      unsigned type; };
template<      > struct interceptor_helper<false> { typedef long long unsigned type; };
typedef interceptor_helper< sizeof(long) >= 8u >::type location_t;

union tree_node;
typedef union tree_node *tree;

extern void start_function_interceptor (tree *pdecl1);
extern void queue_emission_of_interceptor_thunk_if_decl_is_interceptor (tree fndecl);
extern void flush_pending_interceptor_thunks (void);

extern tree handle_interceptor_attribute (tree *node, tree name, tree args, int flags, bool *no_add_attrs);

extern tree build_interceptor_goto_target (location_t loc, tree expr);

#endif // GCC_INTERCEPTOR_H
