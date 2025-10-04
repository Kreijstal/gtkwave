#include <gtkwave.h>
#include <glib/gstdio.h>
#include <glib/gmessages.h>

// Test to reproduce the snapshot update issue
// This simulates:
// 1. Loading initial VCD data with a signal
// 2. Getting a snapshot (simulating first GUI click)
// 3. Adding more VCD data
// 4. Checking if the new snapshot reflects the updated data

static void test_snapshot_update_issue(void)
{
    g_test_message("=== Starting snapshot update test ===");
    
    GwVcdPartialLoader *loader = gw_vcd_partial_loader_new();
    g_assert_nonnull(loader);

    // Initial VCD content with signal up to time 5
    const gchar *vcd_initial = 
        "$timescale 1ns $end\n"
        "$scope module mysim $end\n"
        "$var wire 8 ! sine_wave [7:0] $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "b00000000 !\n"
        "#1\n"
        "b00000001 !\n"
        "#2\n"
        "b00000010 !\n"
        "#3\n"
        "b00000011 !\n"
        "#4\n"
        "b00000100 !\n"
        "#5\n"
        "b00000101 !\n";

    GError *error = NULL;
    
    // Feed initial data
    g_test_message("Feeding initial VCD data (times 0-5)");
    gboolean success = gw_vcd_partial_loader_feed(loader, vcd_initial, strlen(vcd_initial), &error);
    g_assert_no_error(error);
    g_assert_true(success);
    
    // Get dump file and check initial state
    g_test_message("Getting dump file after initial load");
    GwDumpFile *dump_file = gw_vcd_partial_loader_get_dump_file(loader);
    g_assert_nonnull(dump_file);
    
    GwFacs *facs = gw_dump_file_get_facs(dump_file);
    g_assert_nonnull(facs);
    g_assert_cmpint(gw_facs_get_length(facs), ==, 1);
    
    // Get the signal node
    GwFacs *all_facs = gw_dump_file_get_facs(dump_file);
    GwSymbol *symbol = gw_facs_get(all_facs, 0); // Get first (and only) fac
    g_test_message("First fac name: %s", symbol->name);
    g_assert_nonnull(symbol);
    GwNode *node = symbol->n;
    g_assert_nonnull(node);
    
    // Check initial snapshot
    GwNodeHistory *snapshot1 = gw_node_get_history_snapshot(node);
    int numhist1;
    if (snapshot1) {
        numhist1 = gw_node_history_get_numhist(snapshot1);
        g_test_message("First snapshot exists: numhist=%d", numhist1);
        gw_node_history_unref(snapshot1);
    } else {
        numhist1 = node->numhist;
        g_test_message("No snapshot yet, using direct access: numhist=%d", numhist1);
    }
    
    // The initial load should have: 1 head entry + 6 value changes (times 0-5) = 7 total
    // (The head is typically at time -2 or -1 for initialization)
    g_test_message("Expected approximately 7-8 history entries for times 0-5 plus initialization");
    
    // Feed more data (times 6-10)
    const gchar *vcd_more = 
        "#6\n"
        "b00000110 !\n"
        "#7\n"
        "b00000111 !\n"
        "#8\n"
        "b00001000 !\n"
        "#9\n"
        "b00001001 !\n"
        "#10\n"
        "b00001010 !\n";
    
    g_test_message("Feeding more VCD data (times 6-10)");
    success = gw_vcd_partial_loader_feed(loader, vcd_more, strlen(vcd_more), &error);
    g_assert_no_error(error);
    g_assert_true(success);
    
    // Force update by calling get_dump_file
    g_test_message("Getting dump file after feeding more data");
    dump_file = gw_vcd_partial_loader_get_dump_file(loader);
    g_assert_nonnull(dump_file);
    
    // Re-lookup the symbol/node (in case it changed)
    all_facs = gw_dump_file_get_facs(dump_file);
    symbol = gw_facs_get(all_facs, 0);
    g_assert_nonnull(symbol);
    node = symbol->n;
    g_assert_nonnull(node);
    
    // Check snapshot after adding more data
    GwNodeHistory *snapshot2 = gw_node_get_history_snapshot(node);
    int numhist2;
    if (snapshot2) {
        numhist2 = gw_node_history_get_numhist(snapshot2);
        g_test_message("Second snapshot: numhist=%d", numhist2);
        gw_node_history_unref(snapshot2);
    } else {
        numhist2 = node->numhist;
        g_test_message("Still no snapshot, using direct access: numhist=%d", numhist2);
    }
    
    // The snapshot should have grown
    g_test_message("Comparing: initial=%d, after_more_data=%d", numhist1, numhist2);
    g_assert_cmpint(numhist2, >, numhist1);
    g_test_message("PASS: Snapshot was updated with new data");
    
    // Verify we have approximately 12-13 entries now (0-10 plus initialization)
    g_test_message("Expected approximately 12-13 history entries for times 0-10 plus initialization");
    
    // Feed even more data to ensure continuous updates work
    const gchar *vcd_final = 
        "#11\n"
        "b00001011 !\n"
        "#12\n"
        "b00001100 !\n"
        "#13\n"
        "b00001101 !\n";
    
    g_test_message("Feeding final VCD data (times 11-13)");
    success = gw_vcd_partial_loader_feed(loader, vcd_final, strlen(vcd_final), &error);
    g_assert_no_error(error);
    g_assert_true(success);
    
    dump_file = gw_vcd_partial_loader_get_dump_file(loader);
    all_facs = gw_dump_file_get_facs(dump_file);
    symbol = gw_facs_get(all_facs, 0);
    g_assert_nonnull(symbol);
    node = symbol->n;
    
    GwNodeHistory *snapshot3 = gw_node_get_history_snapshot(node);
    int numhist3;
    if (snapshot3) {
        numhist3 = gw_node_history_get_numhist(snapshot3);
        g_test_message("Third snapshot: numhist=%d", numhist3);
        gw_node_history_unref(snapshot3);
    } else {
        numhist3 = node->numhist;
        g_test_message("Still using direct access: numhist=%d", numhist3);
    }
    
    // Verify continuous growth
    g_test_message("Comparing: second=%d, third=%d", numhist2, numhist3);
    g_assert_cmpint(numhist3, >, numhist2);
    g_test_message("PASS: Snapshot continues to update with new data");
    
    // Verify the harray is consistent with numhist
    if (snapshot3) {
        GwHistEnt **harray = gw_node_history_get_harray(snapshot3);
        g_assert_nonnull(harray);
        g_test_message("PASS: Snapshot has valid harray");
        
        // Verify we can access all entries without crash
        for (int i = 0; i < numhist3; i++) {
            g_assert_nonnull(harray[i]);
        }
        g_test_message("PASS: All %d harray entries are accessible", numhist3);
    }
    
    g_object_unref(loader);
    g_test_message("=== Test complete ===");
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    
    g_test_add_func("/vcd-partial-loader/snapshot-update-issue", test_snapshot_update_issue);
    
    return g_test_run();
}
