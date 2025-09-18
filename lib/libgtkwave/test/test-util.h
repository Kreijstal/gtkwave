#pragma once
#include "gw-tree.h"
#include <glib.h>
void assert_tree(GwTreeNode *node, const gchar *expected);
GwTreeNode *get_tree_node(GwTree *tree, const gchar *path);
void put_32(guint8 *p, guint32 v);
void put_8(guint8 *p, guint8 v);
