#include <gtkwave.h>
#include "gw-vcd-stream-loader.h"

static void test_stream_loader_simple(void)
{
    GwLoader *loader = gw_vcd_stream_loader_new();
    g_assert_nonnull(loader);

    const gchar *vcd_part1 = "$version GTKWave created VCD dump $end\n"
                             "$timescale 1ns $end\n"
                             "$scope module top $end\n"
                             "$var wire 1 N clk $end\n"
                             "$upscope $end\n"
                             "$enddefinitions $end\n"
                             "#0\n"
                             "0N\n";

    const gchar *vcd_part2 = "#10\n"
                             "1N\n"
                             "#20\n"
                             "0N\n";

    GError *error = NULL;
    gw_vcd_stream_loader_pump(GW_VCD_STREAM_LOADER(loader), (const guint8 *)vcd_part1, strlen(vcd_part1), &error);
    g_assert_no_error(error);

    gw_vcd_stream_loader_pump(GW_VCD_STREAM_LOADER(loader), (const guint8 *)vcd_part2, strlen(vcd_part2), &error);
    g_assert_no_error(error);

    gw_vcd_stream_loader_eof(GW_VCD_STREAM_LOADER(loader), &error);
    g_assert_no_error(error);

    // In a real test, we would get the dump file and check it
    // GwDumpFile *file = gw_vcd_stream_loader_get_dump_file(GW_VCD_STREAM_LOADER(loader));
    // g_assert_nonnull(file);

    g_object_unref(loader);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/vcd_stream_loader/simple", test_stream_loader_simple);

    return g_test_run();
}
