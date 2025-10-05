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

    // make quick array lookup for aet display--normally this is done in addnode
    if (self->harray == NULL) {
        GwHistEnt *histpnt = &(self->head);
        int histcount = 0;

        while (histpnt) {
            histcount++;
            histpnt = histpnt->next;
        }

        self->numhist = histcount;

        GwHistEnt **harray = g_new(GwHistEnt *, histcount);
        self->harray = harray;

        histpnt = &(self->head);
        for (i = 0; i < histcount; i++) {
            *harray = histpnt;
            harray++;
            histpnt = histpnt->next;
        }
    }

    GwHistEnt *h = &(self->head);
    while (h) {
        if (h->flags & (GW_HIST_ENT_FLAG_REAL | GW_HIST_ENT_FLAG_STRING)) {
            return NULL;
        }
        h = h->next;
    }

    // Check if this node has valid vector history entries
    // If the node is marked as having extended values (extvals=1) with width > 1,
    // but the history entries use scalar storage (h_val) instead of vector storage (h_vector),
    // we cannot expand it. This can happen with:
    // 1. Nodes that were created from streaming data with scalar integer encoding
    // 2. Already-expanded child nodes being re-expanded
    // We detect this by checking if non-special-time history entries have valid h_vector pointers.
    if (width > 1 && self->numhist > 0) {
        g_debug("gw_node_expand: Checking %d history entries for node '%s'", self->numhist, self->nname);
        for (i = 0; i < self->numhist; i++) {
            h = self->harray[i];
            g_debug("  Entry %d: time=%ld, h_vector=%p", i, h->time, h->v.h_vector);
            // Skip special time markers (t=-2, t=-1, and t>=max which use h_val)
            if (h->time >= 0 && h->time < GW_TIME_MAX - 1) {
                // For vector nodes, h_vector should be a valid pointer, not a small integer
                // If h_vector appears to be a small integer value (likely h_val being misinterpreted),
                // then this node's history uses scalar storage and cannot be expanded as a vector
                if (h->v.h_vector == NULL || (guintptr)(h->v.h_vector) < 256) {
                    g_error(
                        "Cannot expand vector '%s': found scalar history data instead of vector data. This can happen with corrupted or improperly streamed VCD files.",
                        self->nname);
                    return NULL; /* Should not be reached */
                }
                // Found at least one valid vector entry, check the rest of the entries too
            }
        }
    }

    // DEBUG(fprintf(stderr,
    //               "Expanding: (%d to %d) for %d bits over %d entries.\n",
    //               msb,
    //               lsb,
    //               width,
    //               n->numhist));

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
        
        // Initialize minimal history for expanded child nodes - they will dynamically
        // extract their data from the parent node's vector history at render time
        narray[i]->head.time = -1;
        narray[i]->head.v.h_val = GW_BIT_X;
        narray[i]->head.next = NULL;
        narray[i]->curr = &(narray[i]->head);
        narray[i]->numhist = 0; // Will be populated on-demand during rendering
        narray[i]->harray = NULL; // Will be populated on-demand during rendering
    }

    // Skip copying history for expanded nodes - they will dynamically reference parent
    // This allows real-time streaming to work correctly as child nodes stay synchronized
    // with parent updates
    return rc;

    // Old code that copied history - removed to enable live updates
    #if 0
    for (i = 0; i < self->numhist; i++) {
        h = self->harray[i];
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

    for (i = 0; i < width; i++) {
        narray[i]->harray = g_new0(GwHistEnt *, narray[i]->numhist);
        GwHistEnt *htemp = &(narray[i]->head);
        for (gint j = 0; j < narray[i]->numhist; j++) {
            narray[i]->harray[j] = htemp;
            htemp = htemp->next;
        }
    }

    return rc;
    #endif // End of old history copying code
}
