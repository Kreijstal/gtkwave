#pragma once

#include "gw-tree.h"
#include "gw-node.h"

void assert_tree(GwTreeNode *node, const gchar *expected);
GwTreeNode *get_tree_node(GwTree *tree, const gchar *path);
void rebuild_harray_from_list(GwNode *nd);