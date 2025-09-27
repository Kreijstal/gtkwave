#include "gw-node-history.h"
#include <string.h>
#include "gw-bit.h"

GwNodeHistory* gw_node_history_new(void) {
    GwNodeHistory *history = g_new0(GwNodeHistory, 1);
    g_static_rec_mutex_init(&history->lock);
    history->ref_count = 1;

    // t=-2 placeholder (head)
    history->head.time = -2;
    history->head.v.h_val = GW_BIT_X;

    // t=-1 placeholder
    GwHistEnt *h_minus_1 = g_new0(GwHistEnt, 1);
    h_minus_1->time = -1;
    h_minus_1->v.h_val = GW_BIT_X;

    history->head.next = h_minus_1;
    history->curr = h_minus_1;
    history->numhist = 2; // head + h_minus_1

    history->last_time = -1;
    history->last_time_raw = -1;
    return history;
}

void gw_node_history_ref(GwNodeHistory *history) {
    if (history) {
        g_atomic_int_inc(&history->ref_count);
    }
}

void gw_node_history_unref(GwNodeHistory *history) {
    if (history && g_atomic_int_dec_and_test(&history->ref_count)) {
        g_static_rec_mutex_free(&history->lock);

        GwHistEnt *current = history->head.next;
        while (current) {
            GwHistEnt *next = current->next;
            if(current->flags & GW_HIST_ENT_FLAG_STRING) {
                g_free(current->v.h_vector);
            }
            g_free(current);
            current = next;
        }

        g_free(history->harray);
        g_free(history);
    }
}

GwNodeHistory* gw_node_history_copy(GwNodeHistory *src) {
    if (!src) {
        return gw_node_history_new();
    }

    g_static_rec_mutex_lock(&src->lock);

    GwNodeHistory *dest = g_new0(GwNodeHistory, 1);
    g_static_rec_mutex_init(&dest->lock);
    dest->ref_count = 1;

    dest->numhist = src->numhist;
    dest->last_time = src->last_time;
    dest->last_time_raw = src->last_time_raw;
    dest->head = src->head;
    dest->head.next = NULL;

    GwHistEnt *src_curr = &src->head;
    dest->curr = &dest->head;

    while (src_curr->next) {
        src_curr = src_curr->next;
        GwHistEnt *new_ent = g_new0(GwHistEnt, 1);
        *new_ent = *src_curr;
        if((src_curr->flags & GW_HIST_ENT_FLAG_STRING) && src_curr->v.h_vector) {
            new_ent->v.h_vector = g_strdup(src_curr->v.h_vector);
        }
        new_ent->next = NULL;
        dest->curr->next = new_ent;
        dest->curr = new_ent;
    }

    g_static_rec_mutex_unlock(&src->lock);

    if (src->harray && src->numhist > 0) {
        gw_node_history_regenerate_harray(dest);
    }

    return dest;
}

void gw_node_history_append_entry(GwNodeHistory *history, GwHistEnt *entry) {
    g_static_rec_mutex_lock(&history->lock);
    history->curr->next = entry;
    history->curr = entry;
    history->numhist++;
    g_static_rec_mutex_unlock(&history->lock);
}

void gw_node_history_regenerate_harray(GwNodeHistory *history) {
    g_static_rec_mutex_lock(&history->lock);

    g_free(history->harray);
    history->harray = NULL;

    if (history->numhist > 0) {
        history->harray = g_new(GwHistEnt*, history->numhist);
        GwHistEnt *curr = &history->head;
        int i = 0;
        while(curr && i < history->numhist) {
            history->harray[i++] = curr;
            curr = curr->next;
        }
    }

    g_static_rec_mutex_unlock(&history->lock);
}