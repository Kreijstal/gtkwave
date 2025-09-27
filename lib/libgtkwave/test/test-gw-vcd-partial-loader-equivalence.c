#include <gtkwave.h>
#include <glib/gstdio.h>
#include <string.h>
#include "test-helpers.h"

static gchar *read_file_contents(const gchar *filename)
{
    GError *error = NULL;
    gchar *contents;
    gsize length;

    if (!g_file_get_contents(filename, &contents, &length, &error)) {
        g_test_message("Failed to read file %s: %s", filename, error->message);
        g_error_free(error);
        return NULL;
    }

    return contents;
}

static gchar *dump_file_to_string(GwDumpFile *dump_file)
{
    GString *output = g_string_new(NULL);

    g_string_append(output, "Time\n");
    g_string_append(output, "----\n");
    g_string_append_printf(output, "scale: %" GW_TIME_FORMAT "\n", gw_dump_file_get_time_scale(dump_file));

    gchar *dimension = g_enum_to_string(GW_TYPE_TIME_DIMENSION, gw_dump_file_get_time_dimension(dump_file));
    g_string_append_printf(output, "dimension: %s\n", dimension);
    g_free(dimension);

    GwTimeRange *range = gw_dump_file_get_time_range(dump_file);
    g_string_append_printf(output, "range: %" GW_TIME_FORMAT " - %" GW_TIME_FORMAT "\n",
                          gw_time_range_get_start(range), gw_time_range_get_end(range));
    g_string_append_printf(output, "global time offset: %" GW_TIME_FORMAT "\n", gw_dump_file_get_global_time_offset(dump_file));
    g_string_append(output, "\n");

    g_string_append(output, "Tree\n");
    g_string_append(output, "----\n");

    GwTree *tree = gw_dump_file_get_tree(dump_file);
    if (tree && gw_tree_get_root(tree)) {
        GwTreeNode *root = gw_tree_get_root(tree);
        if (root && root->name) {
            gchar *kind = g_enum_to_string(GW_TYPE_TREE_KIND, root->kind);
            g_string_append_printf(output, "%s (kind=%s, t_which=%d)\n", root->name, kind, root->t_which);
            g_free(kind);

            for (GwTreeNode *child = root->child; child != NULL; child = child->next) {
                kind = g_enum_to_string(GW_TYPE_TREE_KIND, child->kind);
                g_string_append_printf(output, "    %s (kind=%s, t_which=%d)\n", child->name, kind, child->t_which);
                g_free(kind);
            }
        }
    } else {
        g_string_append(output, "no tree structure\n");
    }
    g_string_append(output, "\n");

    g_string_append(output, "Facs\n");
    g_string_append(output, "----\n");

    GwFacs *facs = gw_dump_file_get_facs(dump_file);
    if (facs) {
        for (guint i = 0; i < gw_facs_get_length(facs); i++) {
            GwSymbol *symbol = gw_facs_get(facs, i);
            if (symbol && symbol->name) {
                g_string_append_printf(output, "%s\n", symbol->name);

                if (symbol->n) {
                    GwNode *node = symbol->n;
                    g_string_append_printf(output, "    node: %s\n", node->nname);

                    gchar *vartype_str = g_enum_to_string(GW_TYPE_VAR_TYPE, node->vartype);
                    gchar *vardt_str = g_enum_to_string(GW_TYPE_VAR_DATA_TYPE, node->vardt);
                    gchar *vardir_str = g_enum_to_string(GW_TYPE_VAR_DIR, node->vardir);

                    g_string_append_printf(output, "        vartype: %s\n", vartype_str);
                    g_string_append_printf(output, "        vardt: %s\n", vardt_str);
                    g_string_append_printf(output, "        vardir: %s\n", vardir_str);
                    g_string_append_printf(output, "        varxt: %d\n", node->varxt);
                    g_string_append_printf(output, "        extvals: %d\n", node->extvals);
                    g_string_append_printf(output, "        msi, lsi: %d, %d\n", node->msi, node->lsi);

                    GwNodeHistory *history = gw_node_get_history_snapshot(node);
                    if(history) {
                        g_string_append_printf(output, "        numhist: %d\n", history->numhist);
                        g_string_append_printf(output, "        transitions:\n");

                        for (GwHistEnt *hent = &history->head; hent != NULL; hent = hent->next) {
                            g_string_append_printf(output, "            ");

                            if (hent->flags & (GW_HIST_ENT_FLAG_REAL | GW_HIST_ENT_FLAG_STRING)) {
                                if (hent->flags & GW_HIST_ENT_FLAG_STRING) {
                                    if (hent->time == -2) g_string_append_printf(output, "x");
                                    else if (hent->time == -1) g_string_append_printf(output, "?");
                                    else g_string_append_printf(output, "\"%s\"", hent->v.h_vector);
                                } else {
                                    if (hent->time == -2) {
                                        union { double d; guint64 u; } val_union;
                                        val_union.d = hent->v.h_double;
                                        if (val_union.u == 0x7ff8000000000001ULL) g_string_append_printf(output, "x");
                                        else g_string_append_printf(output, "%f", hent->v.h_double);
                                    } else {
                                        g_string_append_printf(output, "%f", hent->v.h_double);
                                    }
                                }
                            } else if (node->msi == node->lsi) {
                                g_string_append_printf(output, "%c", gw_bit_to_char(hent->v.h_val));
                            } else {
                                if (hent->time < 0) {
                                    g_string_append_printf(output, "?");
                                } else {
                                    gint bits = ABS(node->msi - node->lsi) + 1;
                                    for (gint i = 0; i < bits; i++) {
                                        g_string_append_printf(output, "%c", gw_bit_to_char(hent->v.h_vector[i]));
                                    }
                                }
                            }
                            g_string_append_printf(output, " @ %" GW_TIME_FORMAT "\n", hent->time);
                        }
                        gw_node_history_unref(history);
                    }

                    g_free(vartype_str);
                    g_free(vardt_str);
                    g_free(vardir_str);
                }
            }
        }
    }
    g_string_append(output, "\n");

    return g_string_free(output, FALSE);
}

static void test_vcd_equivalence_full(void)
{
    const char *vcd_filepath = "files/equivalence.vcd";
    GError *error = NULL;

    g_test_message("Testing VCD equivalence for file: %s", vcd_filepath);

    gchar *vcd_contents;
    gsize vcd_len;
    g_file_get_contents(vcd_filepath, &vcd_contents, &vcd_len, &error);
    g_assert_no_error(error);

    GwVcdPartialLoader *partial_loader = gw_vcd_partial_loader_new();

    gboolean success = gw_vcd_partial_loader_feed(partial_loader, vcd_contents, vcd_len, &error);
    g_assert_no_error(error);
    g_assert_true(success);
    g_free(vcd_contents);

    GwDumpFile *actual_dump = gw_vcd_partial_loader_get_dump_file(partial_loader);
    g_assert_nonnull(actual_dump);

    GwLoader *original_loader = gw_vcd_loader_new();
    GwDumpFile *expected_dump = gw_loader_load(GW_LOADER(original_loader), vcd_filepath, &error);
    g_assert_no_error(error);
    g_assert_nonnull(expected_dump);

    g_assert_true(gw_dump_file_import_all(expected_dump, &error));
    g_assert_no_error(error);
    g_assert_true(gw_dump_file_import_all(expected_dump, &error));
    g_assert_no_error(error);

    assert_dump_files_equivalent(expected_dump, actual_dump);

    g_object_unref(partial_loader);
    g_object_unref(original_loader);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vcd_partial_loader/equivalence_full", test_vcd_equivalence_full);
    return g_test_run();
}