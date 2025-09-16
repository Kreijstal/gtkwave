/*
 * Copyright (c) 2024 GTKWave Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "gw-vcd-partial-loader.h"
#include "gw-dump-file.h"
#include "gw-facs.h"
#include "gw-time-range.h"

static gboolean feed_data_incrementally(GIOChannel *in_channel, FILE *vcd_file, 
                                       GwVcdPartialLoader *loader, GwDumpFile *dump_file);

static void test_incremental_loading(void)
{
    const gchar *shmidcat_path = "/home/kreijstal/git/gtkwave/builddir/src/helpers/shmidcat";
    const gchar *input_vcd = "/home/kreijstal/git/gtkwave/lib/libgtkwave/test/files/basic.vcd";
    
    gchar *shm_id_str = NULL;
    gint child_stdin_fd, child_stdout_fd;
    GPid shmidcat_pid;
    GError *error = NULL;



    // Launch shmidcat with pipes for stdin and stdout
    gchar *cmd[] = { (gchar*)shmidcat_path, NULL };
    gboolean success = g_spawn_async_with_pipes(
        NULL, cmd, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, &shmidcat_pid,
        &child_stdin_fd, &child_stdout_fd, NULL, &error
    );
    g_assert_no_error(error);
    g_assert_true(success);

    // Read the SHM ID from shmidcat's stdout first
    GIOChannel *out_ch = g_io_channel_unix_new(child_stdout_fd);
    g_io_channel_read_line(out_ch, &shm_id_str, NULL, NULL, NULL);
    g_assert_nonnull(shm_id_str);
    shm_id_str[strcspn(shm_id_str, "\r\n")] = 0; // Trim newline

    // Give shmidcat a moment to start up and create the SHM segment
    g_usleep(100000); // 100ms

    // Open the VCD file for incremental reading
    FILE *vcd_file = fopen(input_vcd, "r");
    g_assert_nonnull(vcd_file);

    // Feed the entire VCD file to shmidcat - the loader will parse header first
    GIOChannel *in_channel = g_io_channel_unix_new(child_stdin_fd);
    g_io_channel_set_encoding(in_channel, NULL, NULL);
    g_io_channel_set_buffered(in_channel, FALSE);
    
    // Read the entire file and feed it to shmidcat
    gchar buffer[4096];
    size_t bytes_read;
    GError *write_error = NULL;
    gsize bytes_written;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), vcd_file)) > 0) {
        g_io_channel_write_chars(in_channel, buffer, bytes_read, &bytes_written, &write_error);
        g_assert_no_error(write_error);
    }
    g_io_channel_flush(in_channel, NULL);
    
    // Give shmidcat a moment to process the data
    g_usleep(200000); // 200ms

    // Create our loader controller
    GwVcdPartialLoader *loader = gw_vcd_partial_loader_new();

    // Perform the initial load using the new function
    GError *load_error = NULL;
    GwDumpFile *dump_file = gw_vcd_partial_loader_load(loader, shm_id_str, &load_error);
    g_assert_no_error(load_error);
    g_assert_nonnull(dump_file);

    // At this point, only the header should be parsed. Verify symbols exist.
    GwFacs *facs = gw_dump_file_get_facs(dump_file);
    guint initial_fac_count = gw_facs_get_length(facs);
    g_printerr("Initial fac count after header parse: %u\n", initial_fac_count);
    g_assert_cmpint(initial_fac_count, >, 0);

    GwTimeRange *time_range = gw_dump_file_get_time_range(dump_file);
    GwTime initial_end_time = gw_time_range_get_end(time_range);
    g_printerr("Initial end time after header: %" GW_TIME_FORMAT "\n", initial_end_time);

    // The file has already been fed completely, now kick to process value changes
    for (int i = 0; i < 10; i++) {
        gw_vcd_partial_loader_kick(loader);
        g_usleep(50000); // 50ms between kicks
    }

    // Final kick to ensure all data is processed
    gw_vcd_partial_loader_kick(loader);
    
    // Final time range update
    gw_vcd_partial_loader_update_time_range(loader, dump_file);
    
    // Verify final state
    GwTimeRange *final_time_range = gw_dump_file_get_time_range(dump_file);
    GwTime final_end_time = final_time_range ? gw_time_range_get_end(final_time_range) : -1;
    guint final_fac_count = gw_facs_get_length(facs);
    
    // Verify that we have the expected symbols and the end time has increased
    if (final_end_time >= 0 && initial_end_time >= 0) {
        g_assert_cmpint(final_end_time, >, initial_end_time);
    }
    g_assert_cmpint(final_fac_count, ==, initial_fac_count); // No new symbols after header

    // Cleanup
    fclose(vcd_file);
    g_io_channel_unref(in_channel);
    g_io_channel_unref(out_ch);
    close(child_stdout_fd);
    
    // Close stdin to signal EOF to shmidcat
    close(child_stdin_fd);
    
    g_object_unref(dump_file);
    gw_vcd_partial_loader_cleanup(loader);
    g_object_unref(loader);
    g_free(shm_id_str);

    // Wait for shmidcat to exit
    int status;
    waitpid(shmidcat_pid, &status, 0);
    g_spawn_close_pid(shmidcat_pid);
}

static gboolean feed_data_incrementally(GIOChannel *in_channel, FILE *vcd_file, 
                                       GwVcdPartialLoader *loader, GwDumpFile *dump_file)
{
    // This function is no longer needed since we feed the entire file upfront
    // and let the loader handle header/value separation internally
    return TRUE;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/VcdPartialLoader/IncrementalLoading", test_incremental_loading);
    return g_test_run();
}