#include "config.h"     // GCC configuration macros (must be included first)
#include "system.h"     // not sure for what but it's needed
#include "coretypes.h"  // location_t
#include "cp-tree.h"    // tree, cp_expr, tsubst_flags_t

extern bool std_tuple_type_p (tree type, tree *args_out);

extern tree check_for_chimera (cp_expr object, tree name, bool template_p, tsubst_flags_t complain);

extern tree chimera_get_nested_typedef_type (tree chim_type, const char* type_name);

extern tree chimera_vec (tree chim_type, char const *const name_of_tuple);

extern bool chimera_tag_type_p (tree type);

extern bool chimera_type_p (tree type, tree *bases_pack);

extern bool chimera_has_option_named_p (tree chim_type, const char* opt_name);

extern bool cp_is_chimera (tree type);

extern tree build_chimeric_member_access(
  location_t loc,
  cp_expr object,
  tree name,
  bool template_p,
  bool preserve_reference,
  tsubst_flags_t complain);
