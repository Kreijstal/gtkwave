/*
 * Integration test to reproduce the node expansion crash
 * 
 * This test reproduces the exact scenario from the bug report:
 * 1. Expand a multi-bit vector into individual bit nodes
 * 2. Try to expand one of the individual bit nodes
 * 3. If a bit node incorrectly has extvals=1 with scalar history,
 *    the expansion will crash when trying to access h->v.h_vector[j]
 */

#include <gtkwave.h>
#include <stdio.h>
#include <string.h>

static void test_node_expansion_crash(void)
{
    // This test reproduces the EXACT crash from the bug report:
    // A node with extvals=1, msi=7, lsi=0 (marked as 8-bit vector)
    // but its history entries use scalar storage (h_val) instead of vector storage (h_vector)
    // 
    // When gw_node_expand() tries to expand this node, it will try to access
    // h->v.h_vector[j] but h->v.h_vector is actually the scalar value h_val
    // misinterpreted as a pointer (e.g., h_val=1 becomes pointer 0x1)
    // 
    // This causes a segmentation fault.
    
    g_print("\n========== CRASH REPRODUCTION TEST ==========\n");
    g_print("Creating a node with INVALID state:\n");
    g_print("  - Marked as 8-bit vector (extvals=1, msi=7, lsi=0)\n");
    g_print("  - But history uses SCALAR values (h_val), not vector pointers (h_vector)\n");
    g_print("This is the exact scenario from the bug report.\n\n");
    
    // Create a node matching the crash scenario
    GwNode *node = g_new0(GwNode, 1);
    node->nname = g_strdup("mysim.sine_wave");
    node->extvals = 1;  // Marked as having extended values (multi-bit)
    node->msi = 7;      // 8 bits (7 down to 0)
    node->lsi = 0;
    node->vartype = 2;  // Integer type
    
    // Set up history with SCALAR values (the bug scenario)
    // These should be vector pointers for an 8-bit node, but they're scalars!
    node->head.time = -2;
    node->head.v.h_val = GW_BIT_X;  // This is OK for special time
    node->head.next = NULL;
    
    GwHistEnt *h1 = g_new0(GwHistEnt, 1);
    h1->time = -1;
    h1->v.h_val = GW_BIT_X;  // This is OK for special time
    h1->next = NULL;
    node->head.next = h1;
    
    // Add NORMAL time entries with SCALAR values (THIS IS THE BUG!)
    // For an 8-bit vector, these should have h_vector pointing to an 8-byte array
    // But instead, they have h_val which is a scalar
    GwHistEnt *h2 = g_new0(GwHistEnt, 1);
    h2->time = 0;
    h2->v.h_val = 0;  // When interpreted as h_vector, this is pointer 0x0 (NULL)
    h2->next = NULL;
    h1->next = h2;
    
    GwHistEnt *h3 = g_new0(GwHistEnt, 1);
    h3->time = 1;
    h3->v.h_val = 1;  // When interpreted as h_vector, this is pointer 0x1 (INVALID!)
    h3->next = NULL;
    h2->next = h3;
    
    GwHistEnt *h4 = g_new0(GwHistEnt, 1);
    h4->time = 2;
    h4->v.h_val = 127;  // When interpreted as h_vector, this is pointer 0x7F (INVALID!)
    h4->next = NULL;
    h3->next = h4;
    
    node->curr = h4;
    node->numhist = 5;  // -2, -1, 0, 1, 2
    
    // Build harray (this is done by expand code normally)
    node->harray = g_new(GwHistEnt *, 5);
    node->harray[0] = &node->head;
    node->harray[1] = h1;
    node->harray[2] = h2;
    node->harray[3] = h3;
    node->harray[4] = h4;
    
    g_print("Node configuration:\n");
    g_print("  name: %s\n", node->nname);
    g_print("  extvals: %d (should be 1 for vectors)\n", node->extvals);
    g_print("  msi: %d, lsi: %d (width = %d)\n", node->msi, node->lsi, ABS(node->msi - node->lsi) + 1);
    g_print("  numhist: %d\n", node->numhist);
    g_print("\nHistory entries:\n");
    for (int i = 0; i < node->numhist; i++) {
        GwHistEnt *h = node->harray[i];
        g_print("  [%d] time=%" GW_TIME_FORMAT ", h_val=%d, h_vector as pointer=%p\n",
                i, h->time, h->v.h_val, (void*)h->v.h_vector);
    }
    
    g_print("\n!!! ATTEMPTING TO EXPAND !!!\n");
    g_print("WITHOUT THE FIX: This will crash with SIGSEGV when trying to access h->v.h_vector[j]\n");
    g_print("WITH THE FIX: This should return NULL gracefully\n\n");
    
    // This is where the crash occurs!
    // The expansion code will try to loop through history entries
    // and for time >= 0, it will try to access h->v.h_vector[j]
    // But h->v.h_vector is actually h_val (a small integer) being misinterpreted as a pointer
    // Accessing memory at address 0x1 or 0x7F will cause a segmentation fault
    GwExpandInfo *expand_info = gw_node_expand(node);
    
    if (expand_info) {
        g_print("UNEXPECTED: Expansion succeeded (should have been rejected or crashed)\n");
        g_print("  Expanded to %d bits\n", expand_info->width);
        gw_expand_info_free(expand_info);
        g_assert_not_reached();  // This shouldn't happen
    } else {
        g_print("SUCCESS: Expansion returned NULL\n");
        g_print("The fix correctly detected scalar history and prevented the crash!\n");
    }
    
    // Cleanup
    g_free(node->harray);
    g_free(node->nname);
    g_free(h1);
    g_free(h2);
    g_free(h3);
    g_free(h4);
    g_free(node);
    
    g_print("\n========== TEST PASSED ==========\n");
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    
    g_test_add_func("/node/expansion_crash_reproduction", test_node_expansion_crash);
    
    return g_test_run();
}
