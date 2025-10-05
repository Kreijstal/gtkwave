#include <glib.h>
#include "gw-vcd-partial-loader.h"
#include "gw-dump-file.h"
#include "gw-facs.h"
#include "gw-node.h"
#include "gw-bit.h"
#include "test-util.h"

// Helper to convert history entry to a string for comparison
static gchar *history_entry_to_string(GwNode *node, GwHistEnt *entry)
{
    if (!entry)
        return NULL;

    if (entry->flags & (GW_HIST_ENT_FLAG_REAL | GW_HIST_ENT_FLAG_STRING)) {
        if (entry->flags & GW_HIST_ENT_FLAG_STRING) {
            return g_strdup(entry->v.h_vector);
        }
        return g_strdup_printf("%f", entry->v.h_double);
    } else if (node->msi == node->lsi) {
        char c = gw_bit_to_char(entry->v.h_val);
        return g_strdup_printf("%c", c);
    } else {
        gint bits = ABS(node->msi - node->lsi) + 1;
        GString *vec_str = g_string_new("");
        for (gint i = 0; i < bits; i++) {
            g_string_append_c(vec_str, gw_bit_to_char(entry->v.h_vector[i]));
        }
        return g_string_free(vec_str, FALSE);
    }
}

static void test_scalar_to_vector_promotion()
{
    GError *error = NULL;
    gchar *file_contents;
    gsize len;
    const gchar *filename = "files/scalar_to_vector_promotion.vcd";
    g_assert_true(g_file_get_contents(filename, &file_contents, &len, &error));
    g_assert_no_error(error);

    GwVcdPartialLoader *loader = gw_vcd_partial_loader_new();
    gw_vcd_partial_loader_feed(loader, file_contents, len, &error);
    g_assert_no_error(error);

    GwDumpFile *dump_file = gw_vcd_partial_loader_get_dump_file(loader);
    g_assert_true(GW_IS_DUMP_FILE(dump_file));

    g_assert_true(gw_dump_file_import_all(dump_file, &error));
    g_assert_no_error(error);

    GwSymbol *vec_symbol = gw_dump_file_lookup_symbol(dump_file, "test.vec[3:0]");
    g_assert_nonnull(vec_symbol);

    GwNode *node = vec_symbol->n;
    g_assert_nonnull(node);

    GwHistEnt *entry = node->head.next;
    g_assert_nonnull(entry); /* t=-2 */

    entry = entry->next;
    g_assert_nonnull(entry); /* t=-1 */

    entry = entry->next;
    g_assert_nonnull(entry); /* t=10 */
    g_assert_cmpint(entry->time, ==, 10);
    gchar *val1 = history_entry_to_string(node, entry);
    g_assert_cmpstr(val1, ==, "0001");
    g_free(val1);

    entry = entry->next;
    g_assert_nonnull(entry); /* t=20 */
    g_assert_cmpint(entry->time, ==, 20);
    gchar *val2 = history_entry_to_string(node, entry);
    g_assert_cmpstr(val2, ==, "1010");
    g_free(val2);

    g_object_unref(dump_file);
    g_object_unref(loader);
    g_free(file_contents);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vcd_partial_loader/scalar_to_vector_promotion", test_scalar_to_vector_promotion);
    return g_test_run();
}