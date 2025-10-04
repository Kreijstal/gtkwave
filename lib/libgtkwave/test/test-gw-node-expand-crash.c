/*
 * Copyright (c) 2024 GTKWave Contributors
 *
 * Test case to reproduce node expansion crash when expanding a vector node
 * that has scalar history entries (h_val) instead of vector entries (h_vector).
 *
 * This simulates the bug found when streaming VCD data where a multi-bit vector
 * has its history stored as scalars but the expansion code tries to access
 * them as vectors.
 */

#include <gtkwave.h>

static void test_expand_vector_with_scalar_history(void)
{
    // Create a node that looks like an 8-bit vector but has scalar history
    GwNode *node = g_new0(GwNode, 1);
    node->nname = g_strdup("mysim.sine_wave");
    node->extvals = 1;  // Mark as having extended values (multi-bit)
    node->msi = 7;
    node->lsi = 0;
    node->vartype = 2;  // Integer type
    
    // Set up history with scalar values (h_val) not vectors (h_vector)
    // This is the problematic scenario
    node->head.time = 0;
    node->head.v.h_val = 0;  // Using scalar, not vector!
    node->head.next = NULL;
    node->curr = &node->head;
    
    // Add a second history entry
    GwHistEnt *h1 = g_new0(GwHistEnt, 1);
    h1->time = 1;
    h1->v.h_val = 1;  // Scalar value, not a vector pointer
    h1->next = NULL;
    node->head.next = h1;
    
    // Add a third history entry
    GwHistEnt *h2 = g_new0(GwHistEnt, 1);
    h2->time = 2;
    h2->v.h_val = 127;  // Scalar value
    h2->next = NULL;
    h1->next = h2;
    
    node->curr = h2;
    node->numhist = 0;  // Will be calculated by expand
    
    // Try to expand - this should NOT crash
    // The function should detect that history entries are scalar, not vector
    GwExpandInfo *expand_info = gw_node_expand(node);
    
    // The expansion should either:
    // 1. Return NULL (cannot expand because history is not in vector format)
    // 2. Return successfully by detecting and handling scalar history
    
    if (expand_info != NULL) {
        // If expansion succeeded, verify it didn't crash
        g_assert_cmpint(expand_info->width, ==, 8);
        g_assert_cmpint(expand_info->msb, ==, 7);
        g_assert_cmpint(expand_info->lsb, ==, 0);
        
        // Clean up
        for (int i = 0; i < expand_info->width; i++) {
            if (expand_info->narray[i]) {
                g_free(expand_info->narray[i]->nname);
                if (expand_info->narray[i]->expansion) {
                    g_free(expand_info->narray[i]->expansion);
                }
                g_free(expand_info->narray[i]);
            }
        }
        gw_expand_info_free(expand_info);
    }
    
    // Clean up
    g_free(node->nname);
    g_free(h1);
    g_free(h2);
    g_free(node);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/node/expand_vector_with_scalar_history", test_expand_vector_with_scalar_history);

    return g_test_run();
}
