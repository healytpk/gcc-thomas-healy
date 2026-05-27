#ifndef GCC_CP_CLASSALLOCA_H
#define GCC_CP_CLASSALLOCA_H

union tree_node;
typedef union tree_node *tree;

extern tree build_classalloca_from_value   (tree value);
extern tree build_classalloca_cleanup_loop (void);

#endif
