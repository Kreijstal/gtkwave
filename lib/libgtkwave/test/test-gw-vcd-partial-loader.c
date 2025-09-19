#include <glib.h>
#include <glib/gstdio.h>
#include "gw-vcd-partial-loader.h"
#include "gw-vcd-loader-private.h" /* For internal access */
#include "gw-vcd-loader.h"
#include "gw-dump-file.h"
#include "gw-facs.h"
#include "gw-symbol.h"
#include "gw-node.h"
#include "gw-time-range.h"
#include "test-util.h"

#define VCD_BSIZ 32768

GwDumpFile *gw_vcd_partial_loader_load_internal(GwVcdPartialLoader *self, GError **error);

/*
 * Test fixture to hold the state for our simulated stream.
 */
typedef struct {
    GwVcdPartialLoader *loader;
    GwDumpFile *dump_file;
    FILE *vcd_file_handle;
    GString *data_buffer; /* This simulates the shared memory buffer */
    gboolean eof;
} TestFixture;

/*
 * This is our custom fetch function. Instead of reading from shared memory,
 * it reads from our string buffer, simulating the data arriving over a pipe.
 */
static int test_getch_fetch(GwVcdLoader *loader)
{
    TestFixture *fixture = (TestFixture *)loader->getch_fetch_override_data;

    if (fixture->data_buffer->len == 0) {
        return -1; /* Signal EOF for this kick */
    }

    size_t len = fixture->data_buffer->len;
    if (len > VCD_BSIZ) {
        len = VCD_BSIZ;
    }

    memcpy(loader->vcdbuf, fixture->data_buffer->str, len);
    g_string_erase(fixture->data_buffer, 0, len); /* Consume the data */

    loader->vend = (loader->vst = loader->vcdbuf) + len;
    return (int)(*loader->vst);
}

/*
 * Helper function to feed one more line from the VCD file into our buffer.
 */
static void feed_one_line(TestFixture *fixture)
{
    if (fixture->eof) return;

    char line_buf[4096];
    if (fgets(line_buf, sizeof(line_buf), fixture->vcd_file_handle)) {
        g_string_append(fixture->data_buffer, line_buf);
    } else {
        fixture->eof = TRUE;
    }
}

static void test_incremental_vcd_loading(TestFixture *fixture, gconstpointer user_data)
{
    const gchar *source_dir = g_getenv("MESON_SOURCE_ROOT");
    gchar *input_vcd_path = g_build_filename(source_dir, "lib", "libgtkwave", "test", "files", "basic.vcd", NULL);

    fixture->vcd_file_handle = fopen(input_vcd_path, "r");
    g_assert_nonnull(fixture->vcd_file_handle);
    g_free(input_vcd_path);

    /* Phase 1: Load the header */
    while (!fixture->eof) {
        feed_one_line(fixture);
        if (strstr(fixture->data_buffer->str, "$enddefinitions")) {
            break;
        }
    }
    
    /* Set up the loader to use our custom fetch function */
    GwVcdLoader *vcd_loader = GW_VCD_LOADER(fixture->loader);
    vcd_loader->getch_fetch_override = test_getch_fetch;
    vcd_loader->getch_fetch_override_data = fixture;

    /* Use our new internal load function */
    GError *error = NULL;
    fixture->dump_file = gw_vcd_partial_loader_load_internal(fixture->loader, &error);
    g_assert_no_error(error);
    g_assert_nonnull(fixture->dump_file);

    /* Assertions after header load */
    GwFacs *facs = gw_dump_file_get_facs(fixture->dump_file);
    g_assert_cmpint(gw_facs_get_length(facs), ==, 12);
    GwTimeRange *time_range = gw_dump_file_get_time_range(fixture->dump_file);
    g_assert_cmpint(gw_time_range_get_end(time_range), ==, 0);

    GwSymbol *bit_symbol = gw_facs_lookup(facs, "variables.bit");
    g_assert_nonnull(bit_symbol);
    g_assert_nonnull(bit_symbol->n);
    /* At this point, harray might be built with only the sentinel */
    rebuild_harray_from_list(bit_symbol->n); /* Custom test helper to force rebuild */
    g_assert_cmpint(bit_symbol->n->numhist, ==, 1);

    /* Phase 2: Feed value changes incrementally */
    gint64 expected_time = 0;
    int expected_hist_count = 1;

    while (!fixture->eof) {
        feed_one_line(fixture);
        if (fixture->data_buffer->len == 0) continue;

        if (fixture->data_buffer->str[0] == '#') {
            expected_time = g_ascii_strtoll(fixture->data_buffer->str + 1, NULL, 10);
            expected_hist_count++;
        }

        /* Process the data in the buffer */
        gw_vcd_partial_loader_kick(fixture->loader);
        gw_vcd_partial_loader_process_pending_data(fixture->loader, fixture->dump_file);

        /* Assertions after each kick */
        time_range = gw_dump_file_get_time_range(fixture->dump_file);
        g_assert_cmpint(gw_time_range_get_end(time_range), ==, expected_time);

        rebuild_harray_from_list(bit_symbol->n); /* Force rebuild for accurate check */
        g_assert_cmpint(bit_symbol->n->numhist, ==, expected_hist_count);
    }

    /* Final check */
    g_assert_cmpint(expected_time, ==, 9);
    g_assert_cmpint(bit_symbol->n->numhist, ==, 13);
}

static void fixture_setup(TestFixture *fixture, gconstpointer user_data)
{
    fixture->loader = gw_vcd_partial_loader_new();
    fixture->data_buffer = g_string_new("");
    fixture->eof = FALSE;
}

static void fixture_teardown(TestFixture *fixture, gconstpointer user_data)
{
    g_string_free(fixture->data_buffer, TRUE);
    if(fixture->vcd_file_handle) fclose(fixture->vcd_file_handle);
    g_clear_object(&fixture->dump_file);
    g_clear_object(&fixture->loader);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add("/VcdPartialLoader/IncrementalLoad", TestFixture, NULL,
               fixture_setup, test_incremental_vcd_loading, fixture_teardown);
    return g_test_run();
}