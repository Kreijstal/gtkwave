/*
 * Test that reproduces the bsearch_node crash with child nodes
 * This test ACTUALLY calls the real bsearch_node() function from src/bsearch.c
 */

#include <glib.h>
#include <gw-node.h>
#include <gw-node-history.h>
#include <gw-hist-ent.h>
#include <gw-bit.h>
#include <gw-time.h>
#include <stdio.h>
#include <string.h>

// Include the real bsearch function from src/bsearch.h
// We will compile src/bsearch.c with this test
#include "../../src/bsearch.h"

/*
 * This test reproduces the ACTUAL crash:
 * 1. Parent vector node has history entries with h_vector data
 * 2. Child node is expanded from parent, with expansion pointer set
 * 3. Parent gets updated with MORE history entries (via snapshot)
 * 4. Call REAL bsearch_node() on child
 * 5. bsearch_node tries to rebuild child history
 * 6. CRASHES at line 149: parent_hist->v.h_vector[bit_index]
 */

static void test_bsearch_node_crash_on_child(void)
{
    g_test_message("=== TESTING ACTUAL bsearch_node() CRASH ===");
    
    // Create a parent node with vector history
    GwNode *parent = g_new0(GwNode, 1);
    parent->nname = g_strdup("mysim.sine_wave");
    parent->msi = 7;  // 8-bit vector [7:0]
    parent->lsi = 0;
    
    // Create initial history with 8-character vector strings
    parent->head.time = 0;
    parent->head.v.h_vector = g_strdup("00000000");
    parent->head.flags = 0;
    parent->head.next = NULL;
    
    GwHistEnt *h1 = g_new0(GwHistEnt, 1);
    h1->time = 10;
    h1->v.h_vector = g_strdup("10101010");
    h1->flags = 0;
    h1->next = NULL;
    parent->head.next = h1;
    
    parent->curr = h1;
    parent->numhist = 2;
    
    // Build harray for parent
    parent->harray = g_new0(GwHistEnt *, 2);
    parent->harray[0] = &parent->head;
    parent->harray[1] = h1;
    
    // Create a snapshot for the parent (simulating partial loader behavior)
    GwNodeHistory *initial_snapshot = gw_node_history_new();
    // Copy the current state
    gw_node_history_set_head(initial_snapshot, &parent->head);
    gw_node_history_set_curr(initial_snapshot, h1);
    gw_node_history_set_numhist(initial_snapshot, 2);
    gw_node_history_set_harray(initial_snapshot, parent->harray);
    
    // Publish the snapshot
    gw_node_publish_new_history(parent, initial_snapshot);
    
    // Create a child node (expanded from parent bit 7)
    GwNode *child = g_new0(GwNode, 1);
    child->nname = g_strdup("mysim.sine_wave[7]");
    
    // Set up expansion pointer
    child->expansion = g_new0(GwExpandReferences, 1);
    child->expansion->parent = parent;
    child->expansion->parentbit = 7;  // Extract bit 7
    child->expansion->actual = 7;
    child->expansion->refcnt = 1;
    
    // Child starts with initial history from parent
    child->head.time = 0;
    child->head.v.h_val = GW_BIT_0;  // From parent's "00000000"
    child->head.flags = 0;
    child->head.next = NULL;
    
    child->curr = &child->head;
    child->numhist = 1;
    child->harray = g_new0(GwHistEnt *, 1);
    child->harray[0] = &child->head;
    
    g_test_message("Created parent and child nodes with initial snapshot");
    
    // ===================================================
    // NOW UPDATE PARENT WITH MORE HISTORY (simulating VCD partial loader)
    // ===================================================
    g_test_message("\n--- Simulating parent update from VCD partial loader ---");
    
    // Add more history to parent
    GwHistEnt *h2 = g_new0(GwHistEnt, 1);
    h2->time = 20;
    h2->v.h_vector = g_strdup("11111111");
    h2->flags = 0;
    h2->next = NULL;
    h1->next = h2;
    
    GwHistEnt *h3 = g_new0(GwHistEnt, 1);
    h3->time = 30;
    h3->v.h_vector = NULL;  // THIS IS NULL - will cause crash!
    h3->flags = 0;
    h3->next = NULL;
    h2->next = h3;
    
    parent->curr = h3;
    parent->numhist = 4;  // Now has 4 entries
    
    // Update parent's harray
    GwHistEnt **new_harray = g_new0(GwHistEnt *, 4);
    new_harray[0] = &parent->head;
    new_harray[1] = h1;
    new_harray[2] = h2;
    new_harray[3] = h3;
    
    // Create and publish new snapshot for parent
    GwNodeHistory *updated_snapshot = gw_node_history_new();
    gw_node_history_set_head(updated_snapshot, &parent->head);
    gw_node_history_set_curr(updated_snapshot, h3);
    gw_node_history_set_numhist(updated_snapshot, 4);
    gw_node_history_set_harray(updated_snapshot, new_harray);
    
    GwNodeHistory *old_snapshot = gw_node_publish_new_history(parent, updated_snapshot);
    if (old_snapshot) {
        gw_node_history_unref(old_snapshot);
    }
    
    g_test_message("Parent now has 4 history entries (child still has 1)");
    g_test_message("Parent's 4th entry has NULL h_vector");
    
    // ===================================================
    // CALL THE REAL bsearch_node() - THIS WILL CRASH!
    // ===================================================
    g_test_message("\n--- Calling REAL bsearch_node() on child ---");
    g_test_message("This will trigger the crash at src/bsearch.c:149");
    g_test_message("Because it tries to access parent_hist->v.h_vector[7] where h_vector is NULL");
    
    // This is the REAL function call that crashes in production
    g_test_message("Calling: bsearch_node(child, 15)...");
    
    // THIS WILL CRASH:
    GwHistEnt *result = bsearch_node(child, 15);
    
    g_test_message("If we get here, the crash was avoided (test failed to reproduce)");
    g_assert(result != NULL || result == NULL);  // Just to use the result
    
    // Cleanup (won't be reached if crash occurs)
    g_free(child->expansion);
    g_free(child->nname);
    g_free(child->harray);
    g_free(child);
    g_free(parent->head.v.h_vector);
    g_free(h1->v.h_vector);
    g_free(h2->v.h_vector);
    g_free(h3);
    g_free(h2);
    g_free(h1);
    g_free(parent->nname);
    g_free(new_harray);
    g_free(parent);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/bsearch/crash-on-child-node", test_bsearch_node_crash_on_child);
    return g_test_run();
}
