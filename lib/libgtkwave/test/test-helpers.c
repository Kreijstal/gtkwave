#include "test-helpers.h"
#include <string.h>
#include "gw-node-history.h"

gchar get_last_value_as_char(GwDumpFile *dump, const gchar *signal_name)
{
    g_assert_nonnull(dump);
    g_assert_nonnull(signal_name);

    GwSymbol *symbol = gw_dump_file_lookup_symbol(dump, signal_name);
    g_assert_nonnull(symbol);
    g_assert_nonnull(symbol->n);

    GwNode *node = symbol->n;
    GwNodeHistory *history = gw_node_get_history_snapshot(node);
    g_assert_nonnull(history);
    g_assert_nonnull(history->curr);

    GwHistEnt *last_hist = history->curr;
    
    gchar result;
    if (node->extvals) {
        if (node->vartype == GW_VAR_TYPE_VCD_REAL) {
            result = 'R';
        } else if (node->vartype == GW_VAR_TYPE_GEN_STRING) {
            if (last_hist->flags & GW_HIST_ENT_FLAG_STRING) {
                result = last_hist->v.h_vector[0];
            } else {
                result = '?';
            }
        } else {
            result = gw_bit_to_char(last_hist->v.h_vector[0]);
        }
    } else {
        result = gw_bit_to_char(last_hist->v.h_val);
    }
    
    gw_node_history_unref(history);
    return result;
}

void assert_history_matches_up_to_time(GwNode *expected_node, GwNode *actual_node, GwTime max_time)
{
    g_assert_nonnull(expected_node);
    g_assert_nonnull(actual_node);

    GwNodeHistory *expected_history = gw_node_get_history_snapshot(expected_node);
    GwNodeHistory *actual_history = gw_node_get_history_snapshot(actual_node);

    g_assert_nonnull(expected_history);
    g_assert_nonnull(actual_history);

    GwHistEnt *expected_hist = expected_history->head.next;
    GwHistEnt *actual_hist = actual_history->head.next;

    while (expected_hist != NULL && actual_hist != NULL) {
        if (actual_hist->time < 0) {
            actual_hist = actual_hist->next;
            continue;
        }

        if (expected_hist->time > max_time) {
            break;
        }

        g_assert_cmpint(actual_hist->time, ==, expected_hist->time);
        g_assert_cmpint(actual_hist->flags, ==, expected_hist->flags);

        if (expected_hist->flags & GW_HIST_ENT_FLAG_STRING) {
            g_assert_cmpstr((const char *)actual_hist->v.h_vector, ==, (const char *)expected_hist->v.h_vector);
        } else if (expected_hist->flags & GW_HIST_ENT_FLAG_REAL) {
            g_assert_cmpfloat(actual_hist->v.h_double, ==, expected_hist->v.h_double);
        } else if (expected_node->extvals) { 
            int width = ABS(expected_node->msi - expected_node->lsi) + 1;
            g_assert_cmpmem(actual_hist->v.h_vector, width, expected_hist->v.h_vector, width);
        } else { 
            g_assert_cmpint(actual_hist->v.h_val, ==, expected_hist->v.h_val);
        }

        expected_hist = expected_hist->next;
        actual_hist = actual_hist->next;
    }

    if (actual_hist != NULL && actual_hist->time <= max_time) {
        g_assert_true(actual_hist->time > max_time);
    }

    gw_node_history_unref(expected_history);
    gw_node_history_unref(actual_history);
}

void assert_signal_history_matches(GwNode *expected_node, GwNode *actual_node)
{
    g_assert_nonnull(expected_node);
    g_assert_nonnull(actual_node);

    GwNodeHistory *expected_history = gw_node_get_history_snapshot(expected_node);
    GwNodeHistory *actual_history = gw_node_get_history_snapshot(actual_node);

    g_assert_nonnull(expected_history);
    g_assert_nonnull(actual_history);

    GwHistEnt *expected_hist = expected_history->head.next;
    GwHistEnt *actual_hist = actual_history->head.next;

    while (expected_hist != NULL && actual_hist != NULL) {
        if (expected_hist->time >= GW_TIME_MAX - 1) {
            expected_hist = NULL;
            continue;
        }
        if (actual_hist->time < 0) {
            actual_hist = actual_hist->next;
            continue;
        }
        
        g_assert_cmpint(actual_hist->time, ==, expected_hist->time);
        g_assert_cmpint(actual_hist->flags, ==, expected_hist->flags);

        if (expected_hist->flags & GW_HIST_ENT_FLAG_STRING) {
            g_assert_cmpstr((const char *)actual_hist->v.h_vector, ==, (const char *)expected_hist->v.h_vector);
        } else if (expected_hist->flags & GW_HIST_ENT_FLAG_REAL) {
            g_assert_cmpfloat(actual_hist->v.h_double, ==, expected_hist->v.h_double);
        } else if (expected_node->extvals) {
            int width = ABS(expected_node->msi - expected_node->lsi) + 1;
            g_assert_cmpmem(actual_hist->v.h_vector, width, expected_hist->v.h_vector, width);
        } else {
            g_assert_cmpint(actual_hist->v.h_val, ==, expected_hist->v.h_val);
        }

        expected_hist = expected_hist->next;
        actual_hist = actual_hist->next;
    }

    g_assert_null(actual_hist);
    g_assert_true(expected_hist == NULL || expected_hist->time >= GW_TIME_MAX - 1);

    gw_node_history_unref(expected_history);
    gw_node_history_unref(actual_history);
}

void assert_dump_files_equivalent(GwDumpFile *expected_dump, GwDumpFile *actual_dump)
{
    g_assert_nonnull(expected_dump);
    g_assert_nonnull(actual_dump);

    GwFacs *expected_facs = gw_dump_file_get_facs(expected_dump);
    GwFacs *actual_facs = gw_dump_file_get_facs(actual_dump);

    g_assert_nonnull(expected_facs);
    g_assert_nonnull(actual_facs);

    guint expected_count = gw_facs_get_length(expected_facs);
    guint actual_count = gw_facs_get_length(actual_facs);
    
    g_assert_cmpint(actual_count, ==, expected_count);

    g_assert_cmpint(gw_dump_file_get_time_scale(actual_dump), ==, 
                    gw_dump_file_get_time_scale(expected_dump));
    g_assert_cmpint(gw_dump_file_get_time_dimension(actual_dump), ==, 
                    gw_dump_file_get_time_dimension(expected_dump));

    for (guint i = 0; i < expected_count; i++) {
        GwSymbol *expected_symbol = gw_facs_get(expected_facs, i);
        g_assert_nonnull(expected_symbol);
        
        GwSymbol *actual_symbol = gw_dump_file_lookup_symbol(actual_dump, expected_symbol->name);
        
        g_assert_nonnull(actual_symbol);
        g_assert_cmpstr(actual_symbol->name, ==, expected_symbol->name);

        assert_signal_history_matches(expected_symbol->n, actual_symbol->n);
    }
}