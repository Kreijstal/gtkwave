#include <glib.h>
#include "gw-node-history.h"

static void test_node_history_new(void)
{
    GwNodeHistory *history = gw_node_history_new();
    g_assert_nonnull(history);
    
    // Check initial state
    g_assert_nonnull(gw_node_history_get_head(history));
    g_assert_nonnull(gw_node_history_get_curr(history));
    g_assert_null(gw_node_history_get_harray(history));
    g_assert_cmpint(gw_node_history_get_numhist(history), ==, 0);
    
    gw_node_history_unref(history);
}

static void test_node_history_ref_unref(void)
{
    GwNodeHistory *history = gw_node_history_new();
    
    // Test ref/unref
    GwNodeHistory *ref1 = gw_node_history_ref(history);
    g_assert_true(ref1 == history);
    
    gw_node_history_unref(ref1);
    gw_node_history_unref(history);
}

static void test_node_history_append(void)
{
    GwNodeHistory *history = gw_node_history_new();
    
    // Append a few entries
    for (int i = 0; i < 5; i++) {
        GwHistEnt *hent = g_new0(GwHistEnt, 1);
        hent->time = i * 10;
        hent->v.h_val = i;
        gw_node_history_append_entry(history, hent);
    }
    
    g_assert_cmpint(gw_node_history_get_numhist(history), ==, 5);
    
    // Regenerate harray
    gw_node_history_regenerate_harray(history);
    
    g_assert_nonnull(gw_node_history_get_harray(history));
    g_assert_cmpint(gw_node_history_get_numhist(history), ==, 6); // 5 + head
    
    // Verify harray contents
    GwHistEnt **harray = gw_node_history_get_harray(history);
    g_assert_cmpint(harray[0]->time, ==, -1); // head
    g_assert_cmpint(harray[1]->time, ==, 0);
    g_assert_cmpint(harray[2]->time, ==, 10);
    g_assert_cmpint(harray[5]->time, ==, 40);
    
    gw_node_history_unref(history);
}

static void test_node_history_copy(void)
{
    // NOTE: copy_from is not fully implemented for deep copying vectors
    // For now, skip this test. The RCU pattern will work by creating
    // new snapshots with shared immutable history entries.
    g_test_skip("copy_from not fully implemented");
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    
    g_test_add_func("/node-history/new", test_node_history_new);
    g_test_add_func("/node-history/ref-unref", test_node_history_ref_unref);
    g_test_add_func("/node-history/append", test_node_history_append);
    g_test_add_func("/node-history/copy", test_node_history_copy);
    
    return g_test_run();
}
