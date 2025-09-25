#include <gtkwave.h>
#include <glib/gstdio.h>
#include <string.h>
#include "test-helpers.h"

static const gchar *vcd_data =
    "$date today $end\n"
    "$timescale 1 ns $end\n"
    "$scope module mysim $end\n"
    "$var integer 8 ! sine_wave $end\n"
    "$upscope $end\n"
    "$enddefinitions $end\n"
    "#0\n"
    "$dumpvars\n"
    "b0 !\n"
    "$end\n"
    "#1\n"
    "b111 !\n"
    "#2\n"
    "b1111 !\n"
    "#3\n"
    "b10111 !\n";

static void test_custom_vcd_parser(void)
{
    GError *error = NULL;
    g_test_message("Testing custom VCD parser");

    GwVcdPartialLoader *partial_loader = gw_vcd_partial_loader_new();
    gboolean success = gw_vcd_partial_loader_feed(partial_loader, vcd_data, -1, &error);
    g_assert_no_error(error);
    g_assert_true(success);

    GwDumpFile *dump_file = gw_vcd_partial_loader_get_dump_file(partial_loader);
    g_assert_nonnull(dump_file);

    g_assert_true(gw_dump_file_import_all(dump_file, &error));
    g_assert_no_error(error);

    GwFacs *facs = gw_dump_file_get_facs(dump_file);
    if (facs) {
        guint num_facs = gw_facs_get_length(facs);
        printf("DUMP FILE SIGNALS: count=%u\n", num_facs);
        for (guint fi = 0; fi < num_facs; fi++) {
            GwSymbol *sym = gw_facs_get(facs, fi);
            if (!sym) {
                printf("  %u: <null symbol>\n", fi);
                continue;
            }
            GwNode *n = sym->n;
            if (!n) {
                printf("  %u: <symbol without node>\n", fi);
                continue;
            }
            const char *name = n->nname ? n->nname : "<unnamed>";
            printf("  %u: %s (node=%p, numhist=%d)\n", fi, name, (void*)n, n->numhist);

            int printed = 0;
            for (GwHistEnt *he = n->head.next; he != NULL; he = he->next) {
                if (he->time < 0) continue;

                if (he->flags & GW_HIST_ENT_FLAG_REAL) {
                    printf("    hist %d: time=%" GW_TIME_FORMAT " real=%f\n", printed + 1, he->time, he->v.h_double);
                } else if (he->flags & GW_HIST_ENT_FLAG_STRING) {
                    printf("    hist %d: time=%" GW_TIME_FORMAT " string=%s\n", printed + 1, he->time, he->v.h_vector ? he->v.h_vector : "<null>");
                } else if (he->v.h_vector) {
                    gint bits = ABS(n->msi - n->lsi) + 1;
                    GString *vec_str = g_string_new("");
                    for (gint i = 0; i < bits; i++) {
                        g_string_append_c(vec_str, gw_bit_to_char(he->v.h_vector[i]));
                    }
                    printf("    hist %d: time=%" GW_TIME_FORMAT " vector=%s\n", printed + 1, he->time, vec_str->str);

                    if (he->time == 1) {
                        g_assert_cmpstr(vec_str->str, ==, "00000111");
                    } else if (he->time == 2) {
                        g_assert_cmpstr(vec_str->str, ==, "00001111");
                    } else if (he->time == 3) {
                        g_assert_cmpstr(vec_str->str, ==, "00010111");
                    }

                    g_string_free(vec_str, TRUE);
                } else {
                    printf("    hist %d: time=%" GW_TIME_FORMAT " val=%c\n", printed + 1, he->time, gw_bit_to_char(he->v.h_val));
                }
                printed++;
            }
            if (printed == 0) {
                printf("    %s: no positive-time history entries available\n", name);
            }
        }
    } else {
        printf("DUMP FILE: no facs available\n");
    }

    g_object_unref(partial_loader);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/custom_vcd_parser/parse_and_print", test_custom_vcd_parser);
    return g_test_run();
}