#include <stdio.h>
#include "gw-node.h"
#include "gw-bit.h"

void gw_expand_info_free(GwExpandInfo *self) {
    g_return_if_fail(self != NULL);

    g_free(self->narray);
    g_free(self);
}

GwExpandInfo *gw_node_expand(GwNode *self)
{
    g_return_val_if_fail(self != NULL, NULL);

    if (!self->extvals) {
        return NULL;
    }

    GwNodeHistory *history = gw_node_get_history_snapshot(self);
    if(!history) return NULL;

    if (history->harray == NULL) {
        gw_node_history_regenerate_harray(history);
    }

    GwHistEnt *h = &(history->head);
    while (h) {
        if (h->flags & (GW_HIST_ENT_FLAG_REAL | GW_HIST_ENT_FLAG_STRING)) {
            gw_node_history_unref(history);
            return NULL;
        }
        h = h->next;
    }

    gint msb = self->msi;
    gint lsb = self->lsi;
    gint width = ABS(msb - lsb) + 1;
    gint delta = msb > lsb ? -1 : 1;
    gint actual = msb;

    GwNode **narray = g_new0(GwNode *, width);

    GwExpandInfo *rc = g_new0(GwExpandInfo, 1);
    rc->narray = narray;
    rc->msb = msb;
    rc->lsb = lsb;
    rc->width = width;

    char *namex = self->nname;

    gint offset = strlen(namex);
    gint i;
    for (i = offset - 1; i >= 0; i--) {
        if (namex[i] == '[')
            break;
    }
    if (i > -1) {
        offset = i;
    }

    gboolean is_2d = FALSE;
    gint curr_row = 0;
    gint curr_bit = 0;
    gint row_delta = 0;
    gint bit_delta = 0;
    gint new_msi = 0;
    gint new_lsi = 0;

    if (i > 3) {
        if (namex[i - 1] == ']') {
            gboolean colon_seen = FALSE;
            gint j = i - 2;
            for (; j >= 0; j--) {
                if (namex[j] == '[') {
                    break;
                } else if (namex[j] == ':') {
                    colon_seen = TRUE;
                }
            }

            if (j > -1 && colon_seen) {
                gint row_hi = 0;
                gint row_lo = 0;
                int items =
                    sscanf(namex + j, "[%d:%d][%d:%d]", &row_hi, &row_lo, &new_msi, &new_lsi);
                if (items == 4) {
                    row_delta = (row_hi > row_lo) ? -1 : 1;
                    bit_delta = (new_msi > new_lsi) ? -1 : 1;
                    curr_row = row_hi;
                    curr_bit = new_msi;
                    is_2d = (((row_lo - row_hi) * row_delta) + 1) *
                                (((new_lsi - new_msi) * bit_delta) + 1) ==
                            width;
                    if (is_2d) {
                        offset = j;
                    }
                }
            }
        }
    }

    gchar *nam = (char *)g_alloca(offset + 20 + 30);
    memcpy(nam, namex, offset);

    for (i = 0; i < width; i++) {
        narray[i] = g_new0(GwNode, 1);
        g_atomic_pointer_set(&narray[i]->active_history, gw_node_history_new());

        if (!is_2d) {
            sprintf(nam + offset, "[%d]", actual);
        } else {
            sprintf(nam + offset, "[%d][%d]", curr_row, curr_bit);
            if (curr_bit == new_lsi) {
                curr_bit = new_msi;
                curr_row += row_delta;
            } else {
                curr_bit += bit_delta;
            }
        }

        gint len = offset + strlen(nam + offset);
        narray[i]->nname = (char *)g_malloc(len + 1);
        strcpy(narray[i]->nname, nam);

        GwExpandReferences *exp1 = g_new0(GwExpandReferences, 1);
        exp1->parent = self;
        exp1->parentbit = i;
        exp1->actual = actual;
        actual += delta;
        narray[i]->expansion = exp1;
    }

    for (i = 0; i < history->numhist; i++) {
        h = history->harray[i];
        if (h->time < 0 || h->time >= GW_TIME_MAX - 1) {
            for (gint j = 0; j < width; j++) {
                GwNodeHistory *child_history = g_atomic_pointer_get(&narray[j]->active_history);
                GwHistEnt *htemp = g_new0(GwHistEnt, 1);
                htemp->v.h_val = GW_BIT_X;
                htemp->time = h->time;
                child_history->curr->next = htemp;
                child_history->curr = htemp;
                child_history->numhist++;
            }
        } else {
            for (gint j = 0; j < width; j++) {
                unsigned char val = h->v.h_vector[j];
                switch (val) {
                    case '0': val = GW_BIT_0; break;
                    case '1': val = GW_BIT_1; break;
                    case 'x': case 'X': val = GW_BIT_X; break;
                    case 'z': case 'Z': val = GW_BIT_Z; break;
                    case 'h': case 'H': val = GW_BIT_H; break;
                    case 'l': case 'L': val = GW_BIT_L; break;
                    case 'u': case 'U': val = GW_BIT_U; break;
                    case 'w': case 'W': val = GW_BIT_W; break;
                    case '-': val = GW_BIT_DASH; break;
                    default: break;
                }

                GwNodeHistory *child_history = g_atomic_pointer_get(&narray[j]->active_history);
                if (child_history->curr->v.h_val != val) {
                    GwHistEnt *htemp = g_new0(GwHistEnt, 1);
                    htemp->v.h_val = val;
                    htemp->time = h->time;
                    child_history->curr->next = htemp;
                    child_history->curr = htemp;
                    child_history->numhist++;
                }
            }
        }
    }

    for (i = 0; i < width; i++) {
        GwNodeHistory *child_history = g_atomic_pointer_get(&narray[i]->active_history);
        gw_node_history_regenerate_harray(child_history);
        gw_node_history_unref(child_history);
    }

    gw_node_history_unref(history);
    return rc;
}

GwNodeHistory* gw_node_get_history_snapshot(GwNode *node)
{
    GwNodeHistory *history = g_atomic_pointer_get(&node->active_history);
    if(history)
    {
        gw_node_history_ref(history);
    }
    return history;
}

GwNodeHistory* gw_node_publish_new_history(GwNode *node, GwNodeHistory *new_history)
{
    gw_node_history_ref(new_history);
    return g_atomic_pointer_exchange(&node->active_history, new_history);
}
