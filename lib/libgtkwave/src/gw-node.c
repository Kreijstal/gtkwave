#include <stdio.h>
#include "gw-node.h"
#include "gw-node-history.h"
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
        // DEBUG(fprintf(stderr, "Nothing to expand\n"));
        return NULL;
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
                    /* printf(">> %d %d %d %d (items = %d)\n", row_hi, row_lo, new_msi, new_lsi,
                     * items); */

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

    // Try to use snapshot if available, otherwise use direct access
    GwNodeHistory *history = gw_node_get_history_snapshot(self);
    GwHistEnt **harray;
    int numhist;
    
    if (history != NULL) {
        // Use snapshot (thread-safe)
        harray = gw_node_history_get_harray(history);
        numhist = gw_node_history_get_numhist(history);
    } else {
        // Fall back to direct access and ensure harray is generated
        if (self->harray == NULL) {
            GwHistEnt *histpnt = &(self->head);
            int histcount = 0;

            while (histpnt) {
                histcount++;
                histpnt = histpnt->next;
            }

            self->numhist = histcount;

            GwHistEnt **harray_temp = g_new(GwHistEnt *, histcount);
            self->harray = harray_temp;

            histpnt = &(self->head);
            for (i = 0; i < histcount; i++) {
                *harray_temp = histpnt;
                harray_temp++;
                histpnt = histpnt->next;
            }
        }
        harray = self->harray;
        numhist = self->numhist;
    }

    // Check if harray is valid
    if (harray == NULL || numhist == 0) {
        if (history != NULL) {
            gw_node_history_unref(history);
        }
        return NULL;
    }

    GwHistEnt *h = &(self->head);
    while (h) {
        if (h->flags & (GW_HIST_ENT_FLAG_REAL | GW_HIST_ENT_FLAG_STRING)) {
            if (history != NULL) {
                gw_node_history_unref(history);
            }
            return NULL;
        }
        h = h->next;
    }

    // DEBUG(fprintf(stderr,
    //               "Expanding: (%d to %d) for %d bits over %d entries.\n",
    //               msb,
    //               lsb,
    //               width,
    //               numhist));

    for (i = 0; i < width; i++) {
        narray[i] = g_new0(GwNode, 1);
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
        exp1->parent = self; /* point to parent */
        exp1->parentbit = i;
        exp1->actual = actual;
        actual += delta;
        narray[i]->expansion = exp1; /* can be safely deleted if expansion set like here */
    }

    for (i = 0; i < numhist; i++) {
        h = harray[i];
        if (h->time < 0 || h->time >= GW_TIME_MAX - 1) {
            for (gint j = 0; j < width; j++) {
                if (narray[j]->curr) {
                    GwHistEnt *htemp = g_new0(GwHistEnt, 1);
                    htemp->v.h_val = GW_BIT_X; /* 'x' */
                    htemp->time = h->time;
                    narray[j]->curr->next = htemp;
                    narray[j]->curr = htemp;
                } else {
                    narray[j]->head.v.h_val = GW_BIT_X; /* 'x' */
                    narray[j]->head.time = h->time;
                    narray[j]->curr = &(narray[j]->head);
                }

                narray[j]->numhist++;
            }
        } else {
            // Safety check: ensure we have valid vector data
            if (h->v.h_vector == NULL) {
                // Skip this entry if no vector data (shouldn't happen for multi-bit signals)
                continue;
            }
            
            for (gint j = 0; j < width; j++) {
                unsigned char val = h->v.h_vector[j];
                switch (val) {
                    case '0':
                        val = GW_BIT_0;
                        break;
                    case '1':
                        val = GW_BIT_1;
                        break;
                    case 'x':
                    case 'X':
                        val = GW_BIT_X;
                        break;
                    case 'z':
                    case 'Z':
                        val = GW_BIT_Z;
                        break;
                    case 'h':
                    case 'H':
                        val = GW_BIT_H;
                        break;
                    case 'l':
                    case 'L':
                        val = GW_BIT_L;
                        break;
                    case 'u':
                    case 'U':
                        val = GW_BIT_U;
                        break;
                    case 'w':
                    case 'W':
                        val = GW_BIT_W;
                        break;
                    case '-':
                        val = GW_BIT_DASH;
                        break;
                    default:
                        break; /* leave val alone as it's been converted already.. */
                }

                // curr will have been established already by 'x' at time: -1
                if (narray[j]->curr->v.h_val != val) {
                    GwHistEnt *htemp = g_new0(GwHistEnt, 1);
                    htemp->v.h_val = val;
                    htemp->time = h->time;
                    narray[j]->curr->next = htemp;
                    narray[j]->curr = htemp;
                    narray[j]->numhist++;
                }
            }
        }
    }

    // Child nodes should NOT have static harrays - they should dynamically
    // derive their values from the parent's snapshot when accessed
    // This ensures they always see the latest data
    // The harray for child nodes will be NULL, signaling to use the parent
    for (i = 0; i < width; i++) {
        // Don't create harray for child nodes - leave NULL
        // bsearch will detect this and use parent snapshot
        narray[i]->harray = NULL;
        narray[i]->numhist = 0;
    }

    // Release the snapshot if we acquired one
    if (history != NULL) {
        gw_node_history_unref(history);
    }

    return rc;
}

GwNodeHistory *gw_node_create_history_snapshot(GwNode *node)
{
    g_return_val_if_fail(node != NULL, NULL);
    
    GwNodeHistory *history = gw_node_history_new();
    
    // Copy the head entry (inline struct)
    gw_node_history_set_head(history, node->head);
    
    // Point to the node's current entry
    gw_node_history_set_curr(history, node->curr);
    
    // Share the linked list chain (don't deep copy, just reference)
    // NOTE: The head is copied as a value, but head.next points to the shared chain
    // This is intentional - the snapshot shares the entries with the node
    
    // Regenerate harray from the shared chain to ensure consistency
    gw_node_history_regenerate_harray(history);
    
    return history;
}

GwNodeHistory *gw_node_get_history_snapshot(GwNode *node)
{
    g_return_val_if_fail(node != NULL, NULL);
    
    // Atomically get the active history pointer
    GwNodeHistory *history = g_atomic_pointer_get(&node->active_history);
    
    if (history != NULL) {
        // Increment reference count before returning
        gw_node_history_ref(history);
    }
    
    return history;
}

GwNodeHistory *gw_node_publish_new_history(GwNode *node, GwNodeHistory *new_history)
{
    g_return_val_if_fail(node != NULL, NULL);
    g_return_val_if_fail(new_history != NULL, NULL);
    
    // Atomically swap the active history pointer and return the old one
    GwNodeHistory *old_history = g_atomic_pointer_exchange(&node->active_history, new_history);
    
    // The caller is responsible for unreffing old_history
    return old_history;
}
