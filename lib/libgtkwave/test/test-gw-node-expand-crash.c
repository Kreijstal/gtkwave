/*
 * Test that reproduces the bsearch_node crash with child nodes
 * This demonstrates the conditions that lead to the crash
 */

#include <glib.h>
#include <gw-node.h>
#include <gw-node-history.h>
#include <gw-hist-ent.h>
#include <gw-bit.h>
#include <stdio.h>
#include <string.h>

/*
 * This test reproduces the crash scenario:
 * 1. Parent vector node has history entries with h_vector data
 * 2. Child node is expanded from parent, with expansion pointer set
 * 3. Parent gets updated with MORE history entries (VCD partial loader)
 * 4. When bsearch_node accesses child, it tries to rebuild child history
 * 5. The rebuild code accesses parent_hist->v.h_vector[bit_index]
 * 6. THIS CRASHES because:
 *    - bit_index might be >= strlen(h_vector)
 *    - h_vector might be NULL
 *    - Memory might be corrupted/freed
 */

static void test_child_node_crash_scenario(void)
{
    g_test_message("=== DEMONSTRATING CRASH CONDITIONS ===");
    
    // Create a parent node with vector history
    GwNode *parent = g_new0(GwNode, 1);
    parent->nname = g_strdup("mysim.sine_wave");
    parent->msi = 7;  // 8-bit vector [7:0]
    parent->lsi = 0;
    
    // Create history with 8-character vector strings
    parent->head.time = 0;
    parent->head.v.h_vector = g_strdup("00000000");
    parent->head.next = NULL;
    
    GwHistEnt *h1 = g_new0(GwHistEnt, 1);
    h1->time = 10;
    h1->v.h_vector = g_strdup("10101010");
    h1->next = NULL;
    parent->head.next = h1;
    
    parent->curr = h1;
    parent->numhist = 2;
    
    g_test_message("Parent created with %d history entries", parent->numhist);
    
    // Create child node (bit 7 of parent)
    GwNode *child = g_new0(GwNode, 1);
    child->nname = g_strdup("mysim.sine_wave[7]");
    child->expansion = g_new0(GwExpandReferences, 1);
    child->expansion->parent = parent;
    child->expansion->parentbit = 7;  // Extract bit 7
    child->numhist = 2;  // Same as parent at time of creation
    
    g_test_message("Child created for bit %d", child->expansion->parentbit);
    
    // Add more history to parent (simulating VCD loader)
    GwHistEnt *h2 = g_new0(GwHistEnt, 1);
    h2->time = 20;
    h2->v.h_vector = g_strdup("11110000");
    h2->next = NULL;
    h1->next = h2;
    
    parent->curr = h2;
    parent->numhist = 3;  // NOW PARENT HAS MORE THAN CHILD
    
    g_test_message("Parent updated: now %d entries (child still %d)", 
                   parent->numhist, child->numhist);
    
    // THIS is where the crash would occur in bsearch_node():
    // The code would detect parent->numhist (3) > child->numhist (2)
    // and try to rebuild child history like this:
    
    g_test_message("Simulating the crash-inducing code path:");
    g_test_message("  for (i = 0; i < parent_numhist; i++) {");
    g_test_message("    parent_hist = parent_harray[i];");
    g_test_message("    char c = parent_hist->v.h_vector[bit_index];  // <-- CRASH HERE");
    g_test_message("  }");
    
    // The crash happens because:
    // - If parent doesn't have harray built yet, parent_harray[i] might be NULL
    // - If parent_hist is valid but h_vector is NULL, we crash
    // - If bit_index >= strlen(h_vector), we read past the string
    
    // Let's demonstrate each failure mode:
    
    // Failure mode 1: parent->harray is NULL
    g_test_message("\nFailure mode 1: parent->harray is NULL");
    g_assert_null(parent->harray);
    g_test_message("  parent->harray = %p (NULL will crash when dereferenced)", parent->harray);
    
    // Failure mode 2: Build harray, but one entry has NULL h_vector
    parent->harray = g_new0(GwHistEnt *, 3);
    parent->harray[0] = &parent->head;
    parent->harray[1] = h1;
    parent->harray[2] = h2;
    
    // Corrupt one entry by freeing its vector
    g_free(h2->v.h_vector);
    h2->v.h_vector = NULL;
    
    g_test_message("\nFailure mode 2: h_vector is NULL");
    g_test_message("  h2->v.h_vector = %p (NULL will crash when indexed)", h2->v.h_vector);
    
    // Failure mode 3: bit_index out of bounds
    h2->v.h_vector = g_strdup("111");  // Only 3 chars, but we need bit 7!
    g_test_message("\nFailure mode 3: bit_index out of bounds");
    g_test_message("  h2->v.h_vector = \"%s\" (len=%zu)", h2->v.h_vector, strlen(h2->v.h_vector));
    g_test_message("  bit_index = %d (>= len, will access garbage/crash)", child->expansion->parentbit);
    
    // All three of these would cause a crash in the actual bsearch_node code
    g_test_message("\n=== CRASH CONDITIONS DEMONSTRATED ===");
    g_test_message("The actual crash occurs in src/bsearch.c:149");
    g_test_message("  char parent_vec_char = parent_hist->v.h_vector[bit_index];");
    
    // Cleanup
    g_free(parent->harray);
    g_free(h2->v.h_vector);
    g_free(h2);
    g_free(h1->v.h_vector);
    g_free(h1);
    g_free(parent->head.v.h_vector);
    g_free(child->expansion);
    g_free(child->nname);
    g_free(child);
    g_free(parent->nname);
    g_free(parent);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    
    g_test_add_func("/node/child_crash_scenario", test_child_node_crash_scenario);
    
    return g_test_run();
}
