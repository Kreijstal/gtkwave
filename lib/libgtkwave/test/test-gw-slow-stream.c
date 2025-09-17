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
 * FROM, OUT of OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
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

static const gchar *vcd_data[] = {
    "$date today $end\n",
    "$timescale 1 ns $end\n",
    "$scope module test $end\n",
    "$var integer 8 ! sine_wave $end\n",
    "$upscope $end\n",
    "$enddefinitions $end\n",
    "#0\n",
    "b0 !\n",
    "#1\n",
    "b10000000 !\n",
    "#2\n",
    "b11111111 !\n",
    "#3\n",
    "b0 !\n",
    NULL
};

static void test_slow_stream(void)
{
    const gchar *build_dir = g_getenv("MESON_BUILD_ROOT");
    gchar *shmidcat_path = g_build_filename(build_dir, "src", "helpers", "shmidcat", NULL);

    gchar *shm_id_str = NULL;
    gint child_stdin_fd, child_stdout_fd;
    GPid shmidcat_pid;
    GError *error = NULL;

    gchar *cmd[] = { shmidcat_path, NULL };
    gboolean success = g_spawn_async_with_pipes(
        NULL, cmd, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, &shmidcat_pid,
        &child_stdin_fd, &child_stdout_fd, NULL, &error
    );
    g_assert_no_error(error);
    g_assert_true(success);

    GIOChannel *out_ch = g_io_channel_unix_new(child_stdout_fd);
    g_io_channel_read_line(out_ch, &shm_id_str, NULL, NULL, NULL);
    g_assert_nonnull(shm_id_str);
    shm_id_str[strcspn(shm_id_str, "\r\n")] = 0;

    g_usleep(100000);

    GIOChannel *in_channel = g_io_channel_unix_new(child_stdin_fd);
    g_io_channel_set_encoding(in_channel, NULL, NULL);
    g_io_channel_set_buffered(in_channel, FALSE);

    // Feed header
    for (int i = 0; i < 6; i++) {
        g_io_channel_write_chars(in_channel, vcd_data[i], -1, NULL, NULL);
    }
    g_io_channel_flush(in_channel, NULL);
    g_usleep(200000);

    GwVcdPartialLoader *loader = gw_vcd_partial_loader_new();
    GError *load_error = NULL;
    GwDumpFile *dump_file = gw_vcd_partial_loader_load(loader, shm_id_str, &load_error);
    g_assert_no_error(load_error);
    g_assert_nonnull(dump_file);

    GwTimeRange *time_range = gw_dump_file_get_time_range(dump_file);
    GwTime current_end_time = gw_time_range_get_end(time_range);
    g_assert_cmpint(current_end_time, ==, 0);

    // Feed data line by line
    for (int i = 6; vcd_data[i] != NULL; i++) {
        g_io_channel_write_chars(in_channel, vcd_data[i], -1, NULL, NULL);
        g_io_channel_flush(in_channel, NULL);
        g_usleep(100000);

        gw_vcd_partial_loader_kick(loader);
        gw_vcd_partial_loader_update_time_range(loader, dump_file);

        time_range = gw_dump_file_get_time_range(dump_file);
        current_end_time = gw_time_range_get_end(time_range);

        if (vcd_data[i][0] == '#') {
            gint64 expected_time = g_ascii_strtoll(vcd_data[i] + 1, NULL, 10);
            g_assert_cmpint(current_end_time, ==, expected_time);
        }
    }

    close(child_stdin_fd);

    g_object_unref(dump_file);
    gw_vcd_partial_loader_cleanup(loader);
    g_object_unref(loader);
    g_free(shm_id_str);

    int status;
    waitpid(shmidcat_pid, &status, 0);
    g_spawn_close_pid(shmidcat_pid);

    g_free(shmidcat_path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/VcdPartialLoader/SlowStream", test_slow_stream);
    return g_test_run();
}
