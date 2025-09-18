/* In lib/libgtkwave/test/test-gw-vcd-interactive-minimal.c */

#include <glib.h>
#include <glib/gstdio.h>
#include "gw-shared-memory.h"
#include "gw-vcd-partial-loader.h"
#include "gw-dump-file.h"
#include "gw-facs.h"
#include "gw-symbol.h"
#include "gw-node.h"
#include "gw-hist-ent.h"
#include "gw-time-range.h"
#include "test-util.h"

#define RING_BUFFER_SIZE (1024 * 1024)

/* Local implementation of put_8/put_32 to avoid linking issues */
static void put_8(guint8 *p, gint offset, guint8 val)
{
    p[offset] = val;
}

static void put_32(guint8 *p, gint offset, guint32 val)
{
    p[offset] = val & 0xff;
    p[offset + 1] = (val >> 8) & 0xff;
    p[offset + 2] = (val >> 16) & 0xff;
    p[offset + 3] = (val >> 24) & 0xff;
}

// Helper to simulate shmidcat writing a block to the ring buffer
static void feed_shm_data(GwSharedMemory *shm, const char *data, gssize *produce_offset) {
    guint8 *shm_data = gw_shared_memory_get_data(shm);
    gsize len = strlen(data);

    put_32(shm_data, *produce_offset + 1, len);
    for (gsize i = 0; i < len; i++) {
        put_8(shm_data, *produce_offset + 5 + i, data[i]);
    }
    put_8(shm_data, *produce_offset, 1); // Mark block as valid

    *produce_offset = (*produce_offset + 5 + len) % RING_BUFFER_SIZE;
}

static void test_interactive_vcd_minimal(void) {
    GError *error = NULL;

    // --- Setup: Find VCD file and create Shared Memory ---
    const gchar *source_dir = g_getenv("MESON_SOURCE_ROOT");
    gchar *input_vcd_path = g_build_filename(source_dir, "lib", "libgtkwave", "test", "files", "basic.vcd", NULL);
    g_assert_true(g_file_test(input_vcd_path, G_FILE_TEST_EXISTS));

    GwSharedMemory *shm = gw_shared_memory_create(RING_BUFFER_SIZE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(shm);

    // --- Main Test Logic ---
    FILE *vcd_file = fopen(input_vcd_path, "r");
    g_assert_nonnull(vcd_file);

    gssize produce_offset = 0;
    GwVcdPartialLoader *loader = NULL;
    GwDumpFile *dump_file = NULL;

    // Feed the header at once
    GString *header = g_string_new("");
    char line_buffer[4096];
    while (fgets(line_buffer, sizeof(line_buffer), vcd_file)) {
        g_string_append(header, line_buffer);
        if (strstr(line_buffer, "$enddefinitions")) {
            break;
        }
    }
    feed_shm_data(shm, header->str, &produce_offset);
    g_string_free(header, TRUE);

    // Load the header
    loader = gw_vcd_partial_loader_new();
    dump_file = gw_vcd_partial_loader_load(loader, gw_shared_memory_get_id(shm), &error);
    g_assert_no_error(error);
    g_assert_nonnull(dump_file);

    // Feed value changes line by line
    while (fgets(line_buffer, sizeof(line_buffer), vcd_file)) {
        feed_shm_data(shm, line_buffer, &produce_offset);
        gw_vcd_partial_loader_kick(loader);
        g_usleep(1000);
    }

    // --- Cleanup ---
    fclose(vcd_file);
    g_free(input_vcd_path);
    gw_shared_memory_free(shm);
    g_object_unref(dump_file);
    g_object_unref(loader);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/InteractiveVCD/Minimal", test_interactive_vcd_minimal);
    return g_test_run();
}
