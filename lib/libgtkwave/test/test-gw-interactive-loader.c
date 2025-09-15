#include "libgtkwave.h"
#include "gw-interactive-loader.h"
#include "gw-vcd-interactive-loader.h"
#include "gw-hierpack.h"

static void test_interactive_loader_interface(void) {
    GwInteractiveLoader *loader = gw_vcd_interactive_loader_new();
    
    // Test interface methods
    g_assert_true(GW_IS_INTERACTIVE_LOADER(loader));
    g_assert_true(gw_interactive_loader_process_data(loader, "$date", 5));
    g_assert_false(gw_interactive_loader_is_complete(loader));
    
    // Test processing more data
    g_assert_true(gw_interactive_loader_process_data(loader, " 2024-01-01", 11));
    g_assert_true(gw_interactive_loader_process_data(loader, "$end", 4));
    
    // Kick processing
    gw_interactive_loader_kick_processing(loader);
    
    // Cleanup
    gw_interactive_loader_cleanup(loader);
    g_assert_true(gw_interactive_loader_is_complete(loader));
    
    g_object_unref(loader);
}

static void test_hierpack_compression(void) {
    GwHierpackContext *ctx = gw_hierpack_context_new();
    
    const char *test_name = "test.module.signal";
    char *compressed = gw_hierpack_compress(ctx, test_name);
    char *decompressed = gw_hierpack_decompress(ctx, compressed, NULL);
    
    g_assert_cmpstr(test_name, ==, decompressed);
    g_assert_true(g_str_has_prefix(compressed, "~"));
    
    g_free(compressed);
    g_free(decompressed);
    gw_hierpack_context_free(ctx);
}

static void test_hierpack_multiple_compression(void) {
    GwHierpackContext *ctx = gw_hierpack_context_new();
    
    const char *names[] = {
        "top.module1.signal_a",
        "top.module1.signal_b", 
        "top.module2.signal_c",
        "top.module2.signal_d"
    };
    
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        char *compressed = gw_hierpack_compress(ctx, names[i]);
        char *decompressed = gw_hierpack_decompress(ctx, compressed, NULL);
        
        g_assert_cmpstr(names[i], ==, decompressed);
        g_assert_true(g_str_has_prefix(compressed, "~"));
        
        g_free(compressed);
        g_free(decompressed);
    }
    
    gw_hierpack_context_free(ctx);
}

static void test_interactive_loader_complete_sequence(void) {
    GwInteractiveLoader *loader = gw_vcd_interactive_loader_new();
    
    // Simulate a complete VCD file sequence
    const char *vcd_sequence[] = {
        "$date 2024-01-01 $end",
        "$version GTKWave $end",
        "$timescale 1ns $end",
        "$scope module top $end",
        "$var wire 1 ! signal $end",
        "$upscope $end",
        "$enddefinitions $end",
        "#0",
        "0!",
        "#10",
        "1!"
    };
    
    for (guint i = 0; i < G_N_ELEMENTS(vcd_sequence); i++) {
        g_assert_true(gw_interactive_loader_process_data(loader, vcd_sequence[i], strlen(vcd_sequence[i])));
    }
    
    // Kick processing to ensure all data is processed
    gw_interactive_loader_kick_processing(loader);
    
    // Allow some time for processing
    g_usleep(100000); // 100ms
    
    // Cleanup
    gw_interactive_loader_cleanup(loader);
    g_object_unref(loader);
}

static void test_interactive_loader_large_data(void) {
    GwInteractiveLoader *loader = gw_vcd_interactive_loader_new();
    
    // Test with larger chunks of data
    GString *large_data = g_string_new(NULL);
    for (int i = 0; i < 1000; i++) {
        g_string_append_printf(large_data, "$var wire 1 ! signal_%d $end\n", i);
    }
    
    g_assert_true(gw_interactive_loader_process_data(loader, large_data->str, large_data->len));
    
    // Process some timestamp data
    g_assert_true(gw_interactive_loader_process_data(loader, "#0\n", 3));
    for (int i = 0; i < 100; i++) {
        g_assert_true(gw_interactive_loader_process_data(loader, "0!\n", 3));
    }
    
    gw_interactive_loader_kick_processing(loader);
    
    // Allow processing time
    g_usleep(50000); // 50ms
    
    gw_interactive_loader_cleanup(loader);
    g_object_unref(loader);
    g_string_free(large_data, TRUE);
}

int main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    
    // Only run interactive loader tests if the feature is enabled
#ifdef HAVE_INTERACTIVE_VCD
    g_test_add_func("/interactive-loader/interface", test_interactive_loader_interface);
    g_test_add_func("/interactive-loader/complete-sequence", test_interactive_loader_complete_sequence);
    g_test_add_func("/interactive-loader/large-data", test_interactive_loader_large_data);
#endif
    
#ifdef HAVE_HIERPACK
    g_test_add_func("/hierpack/compression", test_hierpack_compression);
    g_test_add_func("/hierpack/multiple-compression", test_hierpack_multiple_compression);
#endif
    
    return g_test_run();
}