/*
 * Copyright (c) Tony Bybell 1999-2018.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "globals.h"
#include <config.h>
#include "analyzer.h"
#include "currenttime.h"
#include "symbol.h"
#include "bsearch.h"
#include <gtkwave.h>
#include "strace.h"
#include "gw-node-history.h"
#include <ctype.h>

static int compar_timechain(const void *s1, const void *s2)
{
    GwTime key, obj, delta;
    GwTime *cpos;
    int rv;

    key = *((GwTime *)s1);
    obj = *(cpos = (GwTime *)s2);

    if ((obj <= key) && (obj > GLOBALS->max_compare_time_tc_bsearch_c_1)) {
        GLOBALS->max_compare_time_tc_bsearch_c_1 = obj;
        GLOBALS->max_compare_pos_tc_bsearch_c_1 = cpos;
    }

    delta = key - obj;
    if (delta < 0)
        rv = -1;
    else if (delta > 0)
        rv = 1;
    else
        rv = 0;

    return (rv);
}

int bsearch_timechain(GwTime key)
{
    GLOBALS->max_compare_time_tc_bsearch_c_1 = -2;
    GLOBALS->max_compare_pos_tc_bsearch_c_1 = NULL;

    if (!GLOBALS->strace_ctx->timearray)
        return (-1);

    if (bsearch(&key,
                GLOBALS->strace_ctx->timearray,
                GLOBALS->strace_ctx->timearray_size,
                sizeof(GwTime),
                compar_timechain)) {
        /* nothing, all side effects are in bsearch */
    }

    if ((!GLOBALS->max_compare_pos_tc_bsearch_c_1) ||
        (GLOBALS->max_compare_time_tc_bsearch_c_1 < GLOBALS->shift_timebase)) {
        GLOBALS->max_compare_pos_tc_bsearch_c_1 =
            GLOBALS->strace_ctx->timearray; /* aix bsearch fix */
    }

    return (GLOBALS->max_compare_pos_tc_bsearch_c_1 - GLOBALS->strace_ctx->timearray);
}

/*****************************************************************************************/

static int compar_histent(const void *s1, const void *s2)
{
    GwTime key, obj, delta;
    GwHistEnt *cpos;
    int rv;

    key = *((GwTime *)s1);
    obj = (cpos = (*((GwHistEnt **)s2)))->time;

    if ((obj <= key) && (obj > GLOBALS->max_compare_time_bsearch_c_1)) {
        GLOBALS->max_compare_time_bsearch_c_1 = obj;
        GLOBALS->max_compare_pos_bsearch_c_1 = cpos;
        GLOBALS->max_compare_index = (GwHistEnt **)s2;
    }

    delta = key - obj;
    if (delta < 0)
        rv = -1;
    else if (delta > 0)
        rv = 1;
    else
        rv = 0;

    return (rv);
}

GwHistEnt *bsearch_node(GwNode *n, GwTime key)
{
    GLOBALS->max_compare_time_bsearch_c_1 = -2;
    GLOBALS->max_compare_pos_bsearch_c_1 = NULL;
    GLOBALS->max_compare_index = NULL;

    // Special handling for child nodes (expanded from parent vector)
    if (n->expansion != NULL && n->expansion->parent != NULL) {
        GwNode *parent = n->expansion->parent;
        int bit_index = n->expansion->parentbit;
        
        // Get parent's current snapshot
        GwNodeHistory *parent_snapshot = gw_node_get_history_snapshot(parent);
        GwHistEnt **parent_harray;
        int parent_numhist;
        
        if (parent_snapshot) {
            parent_harray = gw_node_history_get_harray(parent_snapshot);
            parent_numhist = gw_node_history_get_numhist(parent_snapshot);
        } else {
            parent_harray = parent->harray;
            parent_numhist = parent->numhist;
        }
        
        // Check if parent has more history than when child was created
        if (parent_numhist > n->numhist) {
            // Parent has been updated - rebuild child history from parent
            if (n->nname) {
                fprintf(stderr, "BSEARCH: Rebuilding child %s history from parent (%d -> %d entries)\n",
                        n->nname, n->numhist, parent_numhist);
            }
            
            // Free old harray if exists
            if (n->harray) {
                g_free(n->harray);
                n->harray = NULL;
            }
            
            // Walk parent history and extract bits for this child
            // Build new history entries for changed values
            GwHistEnt *prev_child = NULL;
            int child_count = 0;
            
            for (int i = 0; i < parent_numhist; i++) {
                GwHistEnt *parent_hist = parent_harray[i];
                if (!parent_hist || !parent_hist->v.h_vector) {
                    continue;
                }
                
                // Extract the bit for this child
                char parent_vec_char = parent_hist->v.h_vector[bit_index];
                GwBit val = gw_bit_from_char(parent_vec_char);
                
                // Check if value changed from previous
                if (prev_child == NULL || prev_child->v.h_val != val) {
                    GwHistEnt *new_hist = g_new0(GwHistEnt, 1);
                    new_hist->time = parent_hist->time;
                    new_hist->v.h_val = val;
                    new_hist->flags = parent_hist->flags;
                    new_hist->next = NULL;
                    
                    if (prev_child) {
                        prev_child->next = new_hist;
                    } else {
                        // First entry - update head
                        if (n->head.next) {
                            // Clear old linked list
                            GwHistEnt *temp = n->head.next;
                            while (temp) {
                                GwHistEnt *next = temp->next;
                                g_free(temp);
                                temp = next;
                            }
                        }
                        n->head = *new_hist;
                        g_free(new_hist);
                        new_hist = &n->head;
                    }
                    
                    prev_child = new_hist;
                    child_count++;
                }
            }
            
            // Update child's curr pointer
            n->curr = prev_child;
            n->numhist = child_count;
            
            // Rebuild harray
            n->harray = g_new0(GwHistEnt *, child_count);
            GwHistEnt *temp = &n->head;
            for (int i = 0; i < child_count; i++) {
                n->harray[i] = temp;
                temp = temp->next;
            }
        }
        
        if (parent_snapshot) {
            gw_node_history_unref(parent_snapshot);
        }
        
        // Now proceed with normal bsearch using updated child history
    }

    // Try to use thread-safe snapshot if available
    GwNodeHistory *history = gw_node_get_history_snapshot(n);
    
    GwHistEnt **harray;
    int numhist;
    
    if (history != NULL) {
        // Use snapshot (thread-safe)
        harray = gw_node_history_get_harray(history);
        numhist = gw_node_history_get_numhist(history);
        if (n->nname) {
            fprintf(stderr, "BSEARCH: Using snapshot for node %s, numhist=%d, key=%" GW_TIME_FORMAT "\n",
                    n->nname, numhist, key);
        }
    } else {
        // Fall back to direct access (legacy behavior, not thread-safe)
        harray = n->harray;
        numhist = n->numhist;
        if (n->nname) {
            fprintf(stderr, "BSEARCH: Using direct access for node %s, numhist=%d, key=%" GW_TIME_FORMAT "\n",
                    n->nname, numhist, key);
        }
    }
    
    // Perform bsearch only if we have data
    if (harray != NULL && numhist > 0) {
        if (bsearch(&key, harray, numhist, sizeof(GwHistEnt *), compar_histent)) {
            /* nothing, all side effects are in bsearch */
        }
    }

    if ((!GLOBALS->max_compare_pos_bsearch_c_1) ||
        (GLOBALS->max_compare_time_bsearch_c_1 < GW_TIME_CONSTANT(0))) {
        if (harray != NULL && numhist > 1) {
            GLOBALS->max_compare_pos_bsearch_c_1 = harray[1]; /* aix bsearch fix */
            GLOBALS->max_compare_index = &(harray[1]);
        }
    }

    while (GLOBALS->max_compare_pos_bsearch_c_1 &&
           GLOBALS->max_compare_pos_bsearch_c_1->next) /* non-RoSync dumper deglitching fix */
    {
        if (GLOBALS->max_compare_pos_bsearch_c_1->time !=
            GLOBALS->max_compare_pos_bsearch_c_1->next->time)
            break;
        GLOBALS->max_compare_pos_bsearch_c_1 = GLOBALS->max_compare_pos_bsearch_c_1->next;
    }

    // Release the snapshot if we acquired one
    if (history != NULL) {
        gw_node_history_unref(history);
    }

    return (GLOBALS->max_compare_pos_bsearch_c_1);
}

/*****************************************************************************************/

static int compar_vectorent(const void *s1, const void *s2)
{
    GwTime key, obj, delta;
    GwVectorEnt *cpos;
    int rv;

    key = *((GwTime *)s1);
    /* obj=(cpos=(*((GwVectorEnt **)s2)))->time+GLOBALS->shift_timebase; */

    obj = (cpos = (*((GwVectorEnt **)s2)))->time;

    if ((obj <= key) && (obj > GLOBALS->vmax_compare_time_bsearch_c_1)) {
        GLOBALS->vmax_compare_time_bsearch_c_1 = obj;
        GLOBALS->vmax_compare_pos_bsearch_c_1 = cpos;
        GLOBALS->vmax_compare_index = (GwVectorEnt **)s2;
    }

    delta = key - obj;
    if (delta < 0)
        rv = -1;
    else if (delta > 0)
        rv = 1;
    else
        rv = 0;

    return (rv);
}

GwVectorEnt *bsearch_vector(GwBitVector *b, GwTime key)
{
    GLOBALS->vmax_compare_time_bsearch_c_1 = -2;
    GLOBALS->vmax_compare_pos_bsearch_c_1 = NULL;
    GLOBALS->vmax_compare_index = NULL;

    if (bsearch(&key, b->vectors, b->numregions, sizeof(GwVectorEnt *), compar_vectorent)) {
        /* nothing, all side effects are in bsearch */
    }

    if ((!GLOBALS->vmax_compare_pos_bsearch_c_1) ||
        (GLOBALS->vmax_compare_time_bsearch_c_1 < GW_TIME_CONSTANT(0))) {
        /* ignore warning: array index of '1' indexes past the end of an array (that contains 1
         * elements) [-Warray-bounds] */
        /* because this array is allocated with size > that declared in the structure definition via
         * end of structure malloc padding */
        GLOBALS->vmax_compare_pos_bsearch_c_1 = b->vectors[1]; /* aix bsearch fix */
        GLOBALS->vmax_compare_index = &(b->vectors[1]);
    }

    while (GLOBALS->vmax_compare_pos_bsearch_c_1->next) /* non-RoSync dumper deglitching fix */
    {
        if (GLOBALS->vmax_compare_pos_bsearch_c_1->time !=
            GLOBALS->vmax_compare_pos_bsearch_c_1->next->time)
            break;
        GLOBALS->vmax_compare_pos_bsearch_c_1 = GLOBALS->vmax_compare_pos_bsearch_c_1->next;
    }

    return (GLOBALS->vmax_compare_pos_bsearch_c_1);
}

/*****************************************************************************************/

static int compar_trunc(const void *s1, const void *s2)
{
    char *str;
    char vcache[2];
    int key, obj;

    str = (char *)s2;
    key = *((int *)s1);

    vcache[0] = *str;
    vcache[1] = *(str + 1);
    *str = '+';
    *(str + 1) = 0;
    obj = font_engine_string_measure(GLOBALS->wavefont, GLOBALS->trunc_asciibase_bsearch_c_1);
    *str = vcache[0];
    *(str + 1) = vcache[1];

    if ((obj <= key) && (obj > GLOBALS->maxlen_trunc)) {
        GLOBALS->maxlen_trunc = obj;
        GLOBALS->maxlen_trunc_pos_bsearch_c_1 = str;
    }

    return (key - obj);
}

char *bsearch_trunc(char *ascii, int maxlen)
{
    int len;

    if ((maxlen <= 0) || (!ascii) || (!(len = strlen(ascii))))
        return (NULL);

    GLOBALS->maxlen_trunc_pos_bsearch_c_1 = NULL;

    if (GLOBALS->wavefont->is_mono) {
        int adjusted_len = maxlen / GLOBALS->wavefont->mono_width;
        if (adjusted_len)
            adjusted_len--;
        if (GLOBALS->wavefont->mono_width <= maxlen) {
            GLOBALS->maxlen_trunc_pos_bsearch_c_1 = ascii + adjusted_len;
        }
    } else {
        GLOBALS->maxlen_trunc = 0;

        if (bsearch(&maxlen,
                    GLOBALS->trunc_asciibase_bsearch_c_1 = ascii,
                    len,
                    sizeof(char),
                    compar_trunc)) {
            /* nothing, all side effects are in bsearch */
        }
    }

    return (GLOBALS->maxlen_trunc_pos_bsearch_c_1);
}

char *bsearch_trunc_print(char *ascii, int maxlen)
{
    int len;

    if ((maxlen <= 0) || (!ascii) || (!(len = strlen(ascii))))
        return (NULL);

    GLOBALS->maxlen_trunc = 0;
    GLOBALS->maxlen_trunc_pos_bsearch_c_1 = NULL;

    if (bsearch(&maxlen,
                GLOBALS->trunc_asciibase_bsearch_c_1 = ascii,
                len,
                sizeof(char),
                compar_trunc)) {
        /* nothing, all side effects are in bsearch */
    }

    return (GLOBALS->maxlen_trunc_pos_bsearch_c_1);
}

/*****************************************************************************************/
