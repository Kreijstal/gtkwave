/*
 * Test to verify that expanded child nodes stay synchronized with 
 * streaming updates to their parent node.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "lib/libgtkwave/src/gw-node.h"
#include "lib/libgtkwave/src/gw-hist-ent.h"
#include "lib/libgtkwave/src/gw-bit.h"
#include "src/bsearch.h"

// Mock globals needed by bsearch_node
struct Global {
    GwTime max_compare_time_bsearch_c_1;
    GwHistEnt *max_compare_pos_bsearch_c_1;
    GwHistEnt **max_compare_index;
} mock_globals;

struct Global *GLOBALS = &mock_globals;

// Helper to create a vector history entry
static GwHistEnt *create_vector_hist(GwTime time, const char *vector_str, int len)
{
    GwHistEnt *h = g_new0(GwHistEnt, 1);
    h->time = time;
    h->v.h_vector = g_malloc(len);
    memcpy(h->v.h_vector, vector_str, len);
    h->next = NULL;
    return h;
}

int main(int argc, char **argv)
{
    printf("Testing expanded node streaming updates...\n");
    
    // Create a parent node with 4-bit vector
    GwNode *parent = g_new0(GwNode, 1);
    parent->nname = g_strdup("parent[3:0]");
    parent->msi = 3;
    parent->lsi = 0;
    parent->extvals = 1; // Mark as having extended values
    
    // Initialize parent with initial history
    parent->head.time = -1;
    parent->head.v.h_vector = NULL;
    parent->head.next = NULL;
    
    // Add some initial history to parent
    GwHistEnt *h1 = create_vector_hist(0, "0000", 4);
    GwHistEnt *h2 = create_vector_hist(10, "1010", 4);
    GwHistEnt *h3 = create_vector_hist(20, "1111", 4);
    
    parent->head.next = h1;
    h1->next = h2;
    h2->next = h3;
    parent->curr = h3;
    parent->numhist = 0; // Will be built by bsearch_node
    parent->harray = NULL;
    
    printf("Parent node created with initial history at t=0, 10, 20\n");
    
    // Expand the parent to get child nodes
    GwExpandInfo *exp_info = gw_node_expand(parent);
    if (!exp_info) {
        printf("ERROR: Failed to expand parent node\n");
        return 1;
    }
    
    printf("Parent expanded into %d child nodes\n", exp_info->width);
    
    // Verify child nodes were created with expansion references
    for (int i = 0; i < exp_info->width; i++) {
        GwNode *child = exp_info->narray[i];
        if (!child->expansion || child->expansion->parent != parent) {
            printf("ERROR: Child node %d doesn't have proper expansion reference\n", i);
            return 1;
        }
        if (child->expansion->parentbit != i) {
            printf("ERROR: Child node %d has wrong parentbit: %d\n", i, child->expansion->parentbit);
            return 1;
        }
        printf("  Child %d (%s): expansion->parentbit=%d\n", 
               i, child->nname, child->expansion->parentbit);
    }
    
    // Now simulate a bsearch on child nodes BEFORE adding new parent history
    printf("\nSearching children at t=15 with initial parent history...\n");
    for (int i = 0; i < exp_info->width; i++) {
        GwNode *child = exp_info->narray[i];
        GwHistEnt *h = bsearch_node(child, 15);
        if (h) {
            printf("  Child %d at t=15: value=%c\n", i, gw_bit_to_char(h->v.h_val));
        }
    }
    
    // Add more history to the parent (simulating streaming updates)
    printf("\nAdding new history to parent at t=30, 40...\n");
    GwHistEnt *h4 = create_vector_hist(30, "0101", 4);
    GwHistEnt *h5 = create_vector_hist(40, "1100", 4);
    
    h3->next = h4;
    h4->next = h5;
    parent->curr = h5;
    
    // Invalidate parent's harray to simulate streaming update
    if (parent->harray) {
        g_free(parent->harray);
        parent->harray = NULL;
    }
    
    // Now search child nodes at a time AFTER the new history was added
    printf("\nSearching children at t=35 with updated parent history...\n");
    printf("Expected: based on parent value '0101' at t=30\n");
    for (int i = 0; i < exp_info->width; i++) {
        GwNode *child = exp_info->narray[i];
        
        // Invalidate child's harray to force rebuild
        if (child->harray) {
            g_free(child->harray);
            child->harray = NULL;
        }
        
        GwHistEnt *h = bsearch_node(child, 35);
        if (h) {
            char expected = "0101"[i];
            char actual = gw_bit_to_char(h->v.h_val);
            printf("  Child %d at t=35: value=%c (expected=%c) %s\n", 
                   i, actual, expected, (actual == expected) ? "✓" : "✗ FAIL");
            
            if (actual != expected) {
                printf("ERROR: Child node %d did not see updated parent history!\n", i);
                return 1;
            }
        } else {
            printf("ERROR: No history found for child %d at t=35\n", i);
            return 1;
        }
    }
    
    printf("\n✓ All tests passed! Child nodes are properly synchronized with parent updates.\n");
    
    // Cleanup
    gw_expand_info_free(exp_info);
    g_free(parent->nname);
    g_free(parent);
    
    return 0;
}
