/*
 * Copyright (c) 2024 GTKWave Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <glib.h>
#include "gw-vcd-file.h"
#include "gw-node.h"
#include "gw-hist-ent.h"
#include "gw-vcd-file-private.h"
#include <stdio.h>

// Helper function to rebuild harray from linked list (copied from analyzer.c logic)
static void rebuild_harray_from_list(GwNode *nd)
{
    if (nd->harray) {
        free(nd->harray);
        nd->harray = NULL;
    }

    GwHistEnt *histpnt = &(nd->head);
    int histcount = 0;

    while (histpnt) {
        histcount++;
        histpnt = histpnt->next;
    }

    nd->numhist = histcount;

    if (histcount > 0) {
        nd->harray = malloc(histcount * sizeof(GwHistEnt *));
        histpnt = &(nd->head);
        
        for (int i = 0; i < histcount; i++) {
            nd->harray[i] = histpnt;
            histpnt = histpnt->next;
        }
    }
}

// Test basic direct history appending
static void test_direct_append_basic(void)
{
    // Create VCD file and node
    GwVcdFile *vcd_file = g_object_new(GW_TYPE_VCD_FILE, NULL);
    GwNode *node = g_new0(GwNode, 1);
    node->nname = g_strdup("test_node");

    // Append history entries directly
    gw_vcd_file_add_histent_to_node(vcd_file, node, 100, GW_BIT_1);
    gw_vcd_file_add_histent_to_node(vcd_file, node, 200, GW_BIT_0);
    gw_vcd_file_add_histent_to_node(vcd_file, node, 300, GW_BIT_X);

    // Verify linked list structure
    g_assert_nonnull(node->head.next);
    g_assert_nonnull(node->head.next->next);
    g_assert_nonnull(node->head.next->next->next);
    g_assert_cmpint(node->head.next->time, ==, -1); // Initial 'x' entry
    g_assert_cmpint(node->head.next->v.h_val, ==, GW_BIT_X);
    g_assert_cmpint(node->head.next->next->time, ==, 100);
    g_assert_cmpint(node->head.next->next->v.h_val, ==, GW_BIT_1);
    g_assert_cmpint(node->head.next->next->next->time, ==, 200);
    g_assert_cmpint(node->head.next->next->next->v.h_val, ==, GW_BIT_0);

    // Verify harray is NULL (should be invalidated)
    g_assert_null(node->harray);

    // Rebuild harray
    rebuild_harray_from_list(node);

    // Verify harray is correct
    g_assert_nonnull(node->harray);
    g_assert_true(node->numhist > 0); // Should have at least one entry
    g_assert_cmpint(node->harray[0]->time, ==, 0); // Head entry should be at time 0

    // Cleanup
    if (node->harray) {
        free(node->harray);
    }
    g_free(node->nname);
    g_free(node);
    g_object_unref(vcd_file);
}

// Test basic direct history appending - simplified version
static void test_direct_append_simple(void)
{
    // Create VCD file and node
    GwVcdFile *vcd_file = g_object_new(GW_TYPE_VCD_FILE, NULL);
    GwNode *node = g_new0(GwNode, 1);
    node->nname = g_strdup("test_node");

    // Append a single history entry
    gw_vcd_file_add_histent_to_node(vcd_file, node, 100, GW_BIT_1);

    // Verify linked list structure
    g_assert_nonnull(node->head.next);
    g_assert_cmpint(node->head.next->time, ==, -1); // Initial 'x' entry
    g_assert_cmpint(node->head.next->v.h_val, ==, GW_BIT_X);
    g_assert_nonnull(node->head.next->next);
    g_assert_cmpint(node->head.next->next->time, ==, 100);
    g_assert_cmpint(node->head.next->next->v.h_val, ==, GW_BIT_1);

    // Verify harray is NULL (should be invalidated)
    g_assert_null(node->harray);

    // Cleanup
    g_free(node->nname);
    g_free(node);
    g_object_unref(vcd_file);
}

// Test glitch handling
static void test_direct_append_glitch(void)
{
    GwVcdFile *vcd_file = g_object_new(GW_TYPE_VCD_FILE, NULL);
    GwNode *node = g_new0(GwNode, 1);
    node->nname = g_strdup("test_node");

    // Append entries with glitch at same time
    gw_vcd_file_add_histent_to_node(vcd_file, node, 100, GW_BIT_1);
    gw_vcd_file_add_histent_to_node(vcd_file, node, 100, GW_BIT_0); // Glitch

    // Verify only one entry at time 100 with glitch flag
    g_assert_nonnull(node->head.next);
    g_assert_nonnull(node->head.next->next);
    g_assert_cmpint(node->head.next->next->time, ==, 100);
    g_assert_cmpint(node->head.next->next->v.h_val, ==, GW_BIT_0);
    g_assert_true(node->head.next->next->flags & GW_HIST_ENT_FLAG_GLITCH);

    // Cleanup
    g_free(node->nname);
    g_free(node);
    g_object_unref(vcd_file);
}

// Test duplicate value suppression
static void test_direct_append_duplicate(void)
{
    GwVcdFile *vcd_file = g_object_new(GW_TYPE_VCD_FILE, NULL);
    GwNode *node = g_new0(GwNode, 1);
    node->nname = g_strdup("test_node");

    // Append same value multiple times
    gw_vcd_file_add_histent_to_node(vcd_file, node, 100, GW_BIT_1);
    gw_vcd_file_add_histent_to_node(vcd_file, node, 200, GW_BIT_1); // Same value
    gw_vcd_file_add_histent_to_node(vcd_file, node, 300, GW_BIT_0); // Different value

    // Verify only two entries were added (initial x, 100, 300 - 200 should be suppressed)
    g_assert_nonnull(node->head.next);
    g_assert_nonnull(node->head.next->next);
    g_assert_nonnull(node->head.next->next->next);
    g_assert_cmpint(node->head.next->time, ==, -1);
    g_assert_cmpint(node->head.next->next->time, ==, 100);
    g_assert_cmpint(node->head.next->next->next->time, ==, 300);
    g_assert_cmpint(node->head.next->next->next->v.h_val, ==, GW_BIT_0);

    // Cleanup
    g_free(node->nname);
    g_free(node);
    g_object_unref(vcd_file);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    
    g_test_add_func("/VcdDirectAppend/Basic", test_direct_append_basic);
    g_test_add_func("/VcdDirectAppend/Simple", test_direct_append_simple);
    g_test_add_func("/VcdDirectAppend/Glitch", test_direct_append_glitch);
    g_test_add_func("/VcdDirectAppend/Duplicate", test_direct_append_duplicate);
    
    return g_test_run();
}