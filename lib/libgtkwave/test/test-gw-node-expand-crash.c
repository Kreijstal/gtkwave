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
    // This reproduces the crash scenario from issue where a vector node
    // has its history stored as scalars (h_val) instead of vectors (h_vector)
    GwNode *node = g_new0(GwNode, 1);
    node->nname = g_strdup("mysim.sine_wave");
    node->extvals = 1;  // Mark as having extended values (multi-bit)
    node->msi = 7;
    node->lsi = 0;
    node->vartype = 2;  // Integer type
    
    // Set up history array to simulate what happens after harray is built
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
    node->numhist = 3;
    
    // Build harray manually to simulate what expand does
    node->harray = g_new(GwHistEnt *, 3);
    node->harray[0] = &node->head;
    node->harray[1] = h1;
    node->harray[2] = h2;
    
    // Try to expand - this should NOT crash
    // The function should detect that history entries are scalar, not vector,
    // and return NULL because the node cannot be expanded safely
    GwExpandInfo *expand_info = gw_node_expand(node);
    
    // With the fix, expansion should return NULL because history is in scalar format
    g_assert_null(expand_info);
    
    // Clean up
    g_free(node->harray);
    g_free(node->nname);
    g_free(h1);
    g_free(h2);
    g_free(node);
}

static void test_expand_vector_with_proper_vector_history(void)
{
    // Test that proper vector nodes with h_vector history still expand correctly
    GwNode *node = g_new0(GwNode, 1);
    node->nname = g_strdup("test.vector");
    node->extvals = 1;
    node->msi = 3;
    node->lsi = 0;
    
    // Set up proper vector history with required time=-2 and time=-1 entries
    node->head.time = -2;
    node->head.v.h_val = GW_BIT_X;  // Special time entries use h_val
    node->head.next = NULL;
    
    GwHistEnt *h_init = g_new0(GwHistEnt, 1);
    h_init->time = -1;
    h_init->v.h_val = GW_BIT_X;  // Special time entries use h_val
    h_init->next = NULL;
    node->head.next = h_init;
    
    // Now add normal vector history entries
    GwHistEnt *h1 = g_new0(GwHistEnt, 1);
    h1->time = 0;
    h1->v.h_vector = g_malloc(4);
    h1->v.h_vector[0] = GW_BIT_0;
    h1->v.h_vector[1] = GW_BIT_0;
    h1->v.h_vector[2] = GW_BIT_0;
    h1->v.h_vector[3] = GW_BIT_0;
    h1->next = NULL;
    h_init->next = h1;
    
    GwHistEnt *h2 = g_new0(GwHistEnt, 1);
    h2->time = 1;
    h2->v.h_vector = g_malloc(4);
    h2->v.h_vector[0] = GW_BIT_1;
    h2->v.h_vector[1] = GW_BIT_0;
    h2->v.h_vector[2] = GW_BIT_1;
    h2->v.h_vector[3] = GW_BIT_0;
    h2->next = NULL;
    h1->next = h2;
    
    node->curr = h2;
    node->numhist = 4;  // -2, -1, 0, 1
    
    node->harray = g_new(GwHistEnt *, 4);
    node->harray[0] = &node->head;
    node->harray[1] = h_init;
    node->harray[2] = h1;
    node->harray[3] = h2;
    
    // This should succeed because we have proper vector history
    GwExpandInfo *expand_info = gw_node_expand(node);
    g_assert_nonnull(expand_info);
    g_assert_cmpint(expand_info->width, ==, 4);
    g_assert_cmpint(expand_info->msb, ==, 3);
    g_assert_cmpint(expand_info->lsb, ==, 0);
    
    // Clean up expanded nodes
    for (int i = 0; i < expand_info->width; i++) {
        if (expand_info->narray[i]) {
            // Clean up any history created during expansion
            GwHistEnt *h = expand_info->narray[i]->head.next;
            while (h) {
                GwHistEnt *next = h->next;
                g_free(h);
                h = next;
            }
            g_free(expand_info->narray[i]->nname);
            g_free(expand_info->narray[i]->expansion);
            g_free(expand_info->narray[i]);
        }
    }
    gw_expand_info_free(expand_info);
    
    // Clean up original node
    g_free(node->harray);
    g_free(node->nname);
    g_free(h1->v.h_vector);
    g_free(h2->v.h_vector);
    g_free(h_init);
    g_free(h1);
    g_free(h2);
    g_free(node);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/node/expand_vector_with_scalar_history", test_expand_vector_with_scalar_history);
    g_test_add_func("/node/expand_vector_with_proper_vector_history", test_expand_vector_with_proper_vector_history);

    return g_test_run();
}
