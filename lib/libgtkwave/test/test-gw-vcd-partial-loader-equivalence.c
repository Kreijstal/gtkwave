#include <gtkwave.h>
#include <glib/gstdio.h>
#include "test-helpers.h"

static void test_vcd_equivalence(void)
{
    const char *vcd_filepath = "files/equivalence.vcd";
    GError *error = NULL;

    g_test_message("Testing VCD equivalence for file: %s", vcd_filepath);

    // --- 1. Load with the ORIGINAL, trusted loader to get the "expected" result ---
    g_test_message("Loading with original VCD loader...");
    GwLoader *original_loader = gw_vcd_loader_new();
    GwDumpFile *expected_dump = gw_loader_load(original_loader, vcd_filepath, &error);
    g_assert_no_error(error);
    g_assert_nonnull(expected_dump);
    g_object_unref(original_loader);

    g_test_message("Original loader completed successfully");

    // --- 2. Load with the NEW partial loader to get the "actual" result ---
    g_test_message("Loading with partial VCD loader...");
    gchar *vcd_contents;
    gsize vcd_len;
    g_file_get_contents(vcd_filepath, &vcd_contents, &vcd_len, &error);
    g_assert_no_error(error);

    GwVcdPartialLoader *partial_loader = gw_vcd_partial_loader_new();
    
    // Feed the whole file in one chunk for this test
    gboolean success = gw_vcd_partial_loader_feed(partial_loader, vcd_contents, vcd_len, &error);
    g_assert_no_error(error);
    g_assert_true(success);
    g_free(vcd_contents);

    // Get the live dump file view
    GwDumpFile *actual_dump = gw_vcd_partial_loader_get_dump_file(partial_loader);
    g_assert_nonnull(actual_dump);

    g_test_message("Partial loader completed successfully");

    // --- Debug: Print symbols from both loaders ---
    g_test_message("--- Expected Symbols (Original Loader) ---");
    GwFacs *expected_facs = gw_dump_file_get_facs(expected_dump);
    for (guint i = 0; i < gw_facs_get_length(expected_facs); i++) {
        GwSymbol *sym = gw_facs_get(expected_facs, i);
        g_test_message("[%d]: '%s'", i, sym->name);
    }

    g_test_message("--- Actual Symbols (Partial Loader) ---");
    GwFacs *actual_facs = gw_dump_file_get_facs(actual_dump);
    for (guint i = 0; i < gw_facs_get_length(actual_facs); i++) {
        GwSymbol *sym = gw_facs_get(actual_facs, i);
        g_test_message("[%d]: '%s'", i, sym->name);
    }

    // --- 3. Compare the two dump files ---
    g_test_message("Comparing dump files for equivalence...");
    assert_dump_files_equivalent(expected_dump, actual_dump);

    g_test_message("Equivalence test passed!");

    // --- Cleanup ---
    g_object_unref(expected_dump);
    g_object_unref(partial_loader); // This will also free the live dump view
}

static void test_vcd_equivalence_streaming(void)
{
    const char *vcd_filepath = "files/equivalence.vcd";
    GError *error = NULL;

    g_test_message("Testing VCD equivalence with streaming...");

    // --- 1. Load with the ORIGINAL, trusted loader to get the "expected" result ---
    g_test_message("Loading with original VCD loader...");
    GwLoader *original_loader = gw_vcd_loader_new();
    GwDumpFile *expected_dump = gw_loader_load(original_loader, vcd_filepath, &error);
    g_assert_no_error(error);
    g_assert_nonnull(expected_dump);
    g_object_unref(original_loader);

    // --- 2. Load with the NEW partial loader using streaming ---
    g_test_message("Loading with partial VCD loader using streaming...");
    gchar *vcd_contents;
    gsize vcd_len;
    g_file_get_contents(vcd_filepath, &vcd_contents, &vcd_len, &error);
    g_assert_no_error(error);

    GwVcdPartialLoader *partial_loader = gw_vcd_partial_loader_new();
    
    // Feed the file in chunks to simulate streaming
    const gsize chunk_size = 64; // Small chunks to test streaming behavior
    for (gsize offset = 0; offset < vcd_len; offset += chunk_size) {
        gsize remaining = vcd_len - offset;
        gsize this_chunk = remaining < chunk_size ? remaining : chunk_size;
        
        gboolean success = gw_vcd_partial_loader_feed(partial_loader, 
                                                     vcd_contents + offset, 
                                                     this_chunk, 
                                                     &error);
        g_assert_no_error(error);
        g_assert_true(success);
    }
    g_free(vcd_contents);

    // Get the live dump file view
    GwDumpFile *actual_dump = gw_vcd_partial_loader_get_dump_file(partial_loader);
    g_assert_nonnull(actual_dump);

    g_test_message("Partial loader streaming completed successfully");

    // --- Debug: Print symbols from both loaders ---
    g_test_message("--- Expected Symbols (Original Loader) ---");
    GwFacs *expected_facs = gw_dump_file_get_facs(expected_dump);
    for (guint i = 0; i < gw_facs_get_length(expected_facs); i++) {
        GwSymbol *sym = gw_facs_get(expected_facs, i);
        g_test_message("[%d]: '%s'", i, sym->name);
    }

    g_test_message("--- Actual Symbols (Partial Loader) ---");
    GwFacs *actual_facs = gw_dump_file_get_facs(actual_dump);
    for (guint i = 0; i < gw_facs_get_length(actual_facs); i++) {
        GwSymbol *sym = gw_facs_get(actual_facs, i);
        g_test_message("[%d]: '%s'", i, sym->name);
    }

    // --- 3. Compare the two dump files ---
    g_test_message("Comparing dump files for equivalence after streaming...");
    assert_dump_files_equivalent(expected_dump, actual_dump);

    g_test_message("Streaming equivalence test passed!");

    // --- Cleanup ---
    g_object_unref(expected_dump);
    g_object_unref(partial_loader);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    
    // Add the equivalence tests
    g_test_add_func("/vcd_partial_loader/equivalence", test_vcd_equivalence);
    g_test_add_func("/vcd_partial_loader/equivalence_streaming", test_vcd_equivalence_streaming);
    
    return g_test_run();
}