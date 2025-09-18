/* In lib/libgtkwave/test/test-gw-vcd-interactive.c */

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

// Helper to find a signal by name from the GwFacs list
static GwNode* get_node_by_name(GwDumpFile *dump_file, const char* name) {
    GwFacs *facs = gw_dump_file_get_facs(dump_file);
    gint num_facs = gw_facs_get_length(facs);
    for (gint i = 0; i < num_facs; i++) {
        GwSymbol *s = gw_facs_get(facs, i);
        if (s && s->n && g_strcmp0(s->n->nname, name) == 0) {
            return s->n;
        }
    }
    return NULL;
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

static void test_interactive_vcd_from_file(void) {
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
    GwNode *bit_node = NULL;
    gboolean header_loaded = FALSE;
    gint expected_hist_count = 0;

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

            bit_node = get_node_by_name(dump_file, "bit");
    g_assert_nonnull(bit_node);

    header_loaded = TRUE;
    expected_hist_count = 2; // Expect sentinel head + initial 'x' entry

    // Feed value changes line by line
    while (fgets(line_buffer, sizeof(line_buffer), vcd_file)) {
        if (line_buffer[0] == '#') {
            gint64 current_time = g_ascii_strtoll(&line_buffer[1], NULL, 10);
            g_test_message("Timestamp #%" G_GINT64_FORMAT " detected. Kicking loader.", current_time);

            feed_shm_data(shm, line_buffer, &produce_offset);
            gw_vcd_partial_loader_kick(loader);

            // Import traces to convert vlist to GwHistEnt linked list
            GwNode *nodes_to_import[] = { bit_node, NULL };
            gw_dump_file_import_traces(dump_file, nodes_to_import, &error);
            g_assert_no_error(error);

            // Invalidate and rebuild harray (simulating the fix)
            if (bit_node->harray) free(bit_node->harray);
            bit_node->harray = NULL;

            int hist_count = 0;
            for (GwHistEnt *he = &bit_node->head; he; he = he->next) hist_count++;

            bit_node->numhist = hist_count;
            bit_node->harray = malloc(bit_node->numhist * sizeof(GwHistEnt *));
            GwHistEnt *he = &bit_node->head;
            for (int i = 0; i < bit_node->numhist; i++) {
                bit_node->harray[i] = he;
                he = he->next;
            }

            // Update the dump file's time range
            gw_vcd_partial_loader_update_time_range(loader, dump_file);
            GwTimeRange *time_range = gw_dump_file_get_time_range(dump_file);

            // Assert the state is correct
            if (current_time > 0) {
                 // For #0, dumpvars happens, adding one entry. For subsequent #, one more entry.
                expected_hist_count++;
            }
            g_assert_cmpint(bit_node->numhist, ==, expected_hist_count);
            g_assert_cmpint(gw_time_range_get_end(time_range), ==, current_time);
        } else {
            feed_shm_data(shm, line_buffer, &produce_offset);
        }
    }

    // Final check after the loop
    g_test_message("End of file reached. Final verification.");
    g_assert_true(header_loaded);
    // basic.vcd has values at #0, #1, #2, #3, #4, #5, #6, #7, #8, #9.
    // Total entries = sentinel + initial_x + 10 values = 12
    g_assert_cmpint(bit_node->numhist, ==, 12);

    GwTimeRange *final_time_range = gw_dump_file_get_time_range(dump_file);
    g_assert_cmpint(gw_time_range_get_end(final_time_range), ==, 9);


    // --- Cleanup ---
    fclose(vcd_file);
    g_free(input_vcd_path);
    gw_shared_memory_free(shm);
    g_object_unref(dump_file);
    g_object_unref(loader);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/InteractiveVCD/FromFile", test_interactive_vcd_from_file);
    return g_test_run();
}
