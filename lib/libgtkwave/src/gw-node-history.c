#include "gw-node-history.h"
#include <glib.h>
#include <string.h>
#include <stdint.h>

GwNodeHistory* gw_node_history_new(void) {
    GwNodeHistory *history = g_slice_new0(GwNodeHistory);
    g_rec_mutex_init(&history->lock);
    history->ref_count = 1;
    history->head.time = -2;
    history->head.v.h_val = GW_BIT_X;
    history->curr = &history->head;
    history->numhist = 1;
    history->harray = NULL;
    history->vlist = NULL;
    return history;
}

void gw_node_history_ref(GwNodeHistory *history) {
    if (history) {
        g_atomic_int_inc(&history->ref_count);
    }
}

void gw_node_history_unref(GwNodeHistory *history) {
    if (history && g_atomic_int_dec_and_test(&history->ref_count)) {
        g_rec_mutex_clear(&history->lock);

        GwHistEnt *entry = history->head.next;
        while (entry) {
            GwHistEnt *next = entry->next;
            if (entry->flags & GW_HIST_ENT_FLAG_STRING) {
                g_free(entry->v.h_vector);
            }
            g_slice_free(GwHistEnt, entry);
            entry = next;
        }

        g_free(history->harray);
        if(history->vlist)
        {
            gw_vlist_destroy(history->vlist);
        }
        g_slice_free(GwNodeHistory, history);
    }
}

GwNodeHistory* gw_node_history_copy(const GwNodeHistory *src) {
    if (!src) {
        return NULL;
    }

    GwNodeHistory *new_history = gw_node_history_new();
    gw_node_history_copy_from(new_history, src);
    return new_history;
}

void gw_node_history_copy_from(GwNodeHistory *dest, const GwNodeHistory *src)
{
    if (!dest || !src) {
        return;
    }

    g_rec_mutex_lock((GRecMutex*)&src->lock);
    g_rec_mutex_lock(&dest->lock);

    dest->numhist = src->numhist;

    const GwHistEnt *src_entry = &src->head;
    GwHistEnt *new_curr = &dest->head;

    while (src_entry && src_entry->next) {
        src_entry = src_entry->next;
        GwHistEnt *new_entry = g_slice_new(GwHistEnt);
        memcpy(new_entry, src_entry, sizeof(GwHistEnt));

        if ((src_entry->flags & GW_HIST_ENT_FLAG_STRING) && src_entry->v.h_vector) {
            new_entry->v.h_vector = g_strdup(src_entry->v.h_vector);
        }

        new_entry->next = NULL;
        new_curr->next = new_entry;
        new_curr = new_entry;
    }
    dest->curr = new_curr;

    if (src->harray) {
        gw_node_history_regenerate_harray(dest);
    }

    if(src->vlist)
    {
        dest->vlist = gw_vlist_copy(src->vlist);
    }

    g_rec_mutex_unlock(&dest->lock);
    g_rec_mutex_unlock((GRecMutex*)&src->lock);
}


void gw_node_history_append_entry(GwNodeHistory *history, GwHistEnt *hent) {
    if (!history || !hent) {
        return;
    }
    g_rec_mutex_lock(&history->lock);
    history->curr->next = hent;
    history->curr = hent;
    history->numhist++;
    g_rec_mutex_unlock(&history->lock);
}

void gw_node_history_regenerate_harray(GwNodeHistory *history) {
    if (!history) {
        return;
    }

    g_rec_mutex_lock(&history->lock);

    g_free(history->harray);
    history->harray = NULL;

    if (history->numhist > 0) {
        history->harray = g_new(GwHistEnt*, history->numhist);
        GwHistEnt *entry = &history->head;
        int i = 0;
        while(entry && i < history->numhist) {
            history->harray[i++] = entry;
            entry = entry->next;
        }
    }

    g_rec_mutex_unlock(&history->lock);
}