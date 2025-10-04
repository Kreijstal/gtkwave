/*
 * Copyright (c) 2024 GTKWave Contributors
 *
 * Test case for reproducing a crash when expanding a vector node
 * that has scalar history data mixed with vector data from a VCD file.
 */
#include "test-helpers.h"
#include <gtkwave.h>

static void test_expand_crash_subprocess(void)
{
    const gchar *filename = "files/vector_with_mixed_history.vcd";

    g_assert_true(g_file_test(filename, G_FILE_TEST_EXISTS));

    GError *error = NULL;
    GwLoader *loader = gw_vcd_loader_new();
    GwDumpFile *dump_file = gw_loader_load(loader, filename, &error);

    g_assert_no_error(error);
    g_assert_nonnull(dump_file);

    GwSymbol *symbol = gw_dump_file_lookup_symbol(dump_file, "mysim.sine_wave");
    g_assert_nonnull(symbol);

    GwNode *node = symbol->n;
    g_assert_nonnull(node);

    /* This should crash the subprocess */
    gw_node_expand(node);

    g_object_unref(dump_file);
    g_object_unref(loader);
}

static void test_expand_vector_with_mixed_history_from_vcd(void)
{
    g_test_trap_subprocess("/node/expand_vector_with_mixed_history_from_vcd/subprocess", 0, 0);
    g_test_trap_assert_passed();
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/node/expand_vector_with_mixed_history_from_vcd", test_expand_vector_with_mixed_history_from_vcd);
    g_test_add_func("/node/expand_vector_with_mixed_history_from_vcd/subprocess", test_expand_crash_subprocess);

    return g_test_run();
}