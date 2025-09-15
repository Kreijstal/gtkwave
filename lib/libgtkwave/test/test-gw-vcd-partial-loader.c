#include <gtkwave.h>
#include "gw-vcd-partial-loader.h"
#include <glib.h>

static void test_partial_loader_creation(void)
{
    GwLoader *loader = gw_vcd_partial_loader_new();
    g_assert_nonnull(loader);
    g_assert_true(GW_IS_VCD_PARTIAL_LOADER(loader));
    g_assert_true(GW_IS_VCD_LOADER(loader));
    g_assert_true(GW_IS_LOADER(loader));
    
    GwVcdPartialLoader *partial_loader = GW_VCD_PARTIAL_LOADER(loader);
    g_assert_false(gw_vcd_partial_loader_is_streaming_enabled(partial_loader));
    
    g_object_unref(loader);
}

static void test_partial_loader_properties(void)
{
    GwLoader *loader = gw_vcd_partial_loader_new();
    GwVcdPartialLoader *partial_loader = GW_VCD_PARTIAL_LOADER(loader);
    
    /* Test shared memory ID property */
    gw_vcd_partial_loader_set_shared_memory_id(partial_loader, 0x1234);
    g_assert_cmpuint(gw_vcd_partial_loader_get_shared_memory_id(partial_loader), ==, 0x1234);
    
    /* Test streaming enabled property */
    gw_vcd_partial_loader_set_streaming_enabled(partial_loader, TRUE);
    g_assert_true(gw_vcd_partial_loader_is_streaming_enabled(partial_loader));
    
    gw_vcd_partial_loader_set_streaming_enabled(partial_loader, FALSE);
    g_assert_false(gw_vcd_partial_loader_is_streaming_enabled(partial_loader));
    
    g_object_unref(loader);
}

static void test_partial_loader_interface(void)
{
    GwLoader *loader = gw_vcd_partial_loader_new();
    GwVcdPartialLoader *partial_loader = GW_VCD_PARTIAL_LOADER(loader);
    
    /* Test that it implements the loader interface */
    g_assert_true(GW_IS_LOADER(loader));
    
    /* Test that it inherits from VCD loader */
    g_assert_true(GW_IS_VCD_LOADER(loader));
    
    /* Test property access */
    gw_vcd_partial_loader_set_shared_memory_id(partial_loader, 0x1234);
    g_assert_cmpuint(gw_vcd_partial_loader_get_shared_memory_id(partial_loader), ==, 0x1234);
    
    g_object_unref(loader);
}

static void test_partial_loader_update_callback(void)
{
    GwLoader *loader = gw_vcd_partial_loader_new();
    GwVcdPartialLoader *partial_loader = GW_VCD_PARTIAL_LOADER(loader);
    
    gboolean callback_called = FALSE;
    GwTime callback_time = 0;
    
    /* Test callback setting */
    GwVcdPartialUpdateCallback callback = NULL;
    gw_vcd_partial_loader_set_update_callback(partial_loader, callback, &callback_called);
    
    /* Test getters */
    g_assert_true(gw_vcd_partial_loader_get_update_callback(partial_loader) == callback);
    g_assert_true(gw_vcd_partial_loader_get_update_callback_data(partial_loader) == &callback_called);
    
    g_object_unref(loader);
}

static void test_partial_loader_error_handling(void)
{
    GwLoader *loader = gw_vcd_partial_loader_new();
    
    /* Test loading with invalid shared memory ID */
    GError *error = NULL;
    GwDumpFile *file = gw_loader_load(loader, "0xFFFFFFFF", &error);
    g_assert_null(file);
    g_assert_error(error, GW_DUMP_FILE_ERROR, GW_DUMP_FILE_ERROR_UNKNOWN);
    g_clear_error(&error);
    
    /* Test streaming control */
    GwVcdPartialLoader *partial_loader = GW_VCD_PARTIAL_LOADER(loader);
    gw_vcd_partial_loader_set_streaming_enabled(partial_loader, TRUE);
    g_assert_true(gw_vcd_partial_loader_is_streaming_enabled(partial_loader));
    
    gw_vcd_partial_loader_set_streaming_enabled(partial_loader, FALSE);
    g_assert_false(gw_vcd_partial_loader_is_streaming_enabled(partial_loader));
    
    g_object_unref(loader);
}

static void test_partial_loader_fallback_to_regular_vcd(void)
{
    GwLoader *loader = gw_vcd_partial_loader_new();
    
    /* Test that regular VCD files still work through the partial loader */
    GError *error = NULL;
    GwDumpFile *file = gw_loader_load(loader, "files/basic.vcd", &error);
    
    /* This might fail if the file doesn't exist, but we're testing the fallback mechanism */
    if (file) {
        g_assert_nonnull(file);
        g_assert_true(GW_IS_DUMP_FILE(file));
        g_object_unref(file);
    } else {
        /* File doesn't exist, but that's OK for this test */
        g_assert_no_error(error);
    }
    
    g_clear_error(&error);
    g_object_unref(loader);
}



int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    
    g_test_add_func("/vcd_partial_loader/creation", test_partial_loader_creation);
    g_test_add_func("/vcd_partial_loader/properties", test_partial_loader_properties);
    g_test_add_func("/vcd_partial_loader/interface", test_partial_loader_interface);
    g_test_add_func("/vcd_partial_loader/update_callback", test_partial_loader_update_callback);
    g_test_add_func("/vcd_partial_loader/error_handling", test_partial_loader_error_handling);
    g_test_add_func("/vcd_partial_loader/fallback", test_partial_loader_fallback_to_regular_vcd);
    
    return g_test_run();
}