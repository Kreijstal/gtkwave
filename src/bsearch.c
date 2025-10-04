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

    // Check if this is a child node (expanded from a parent)
    if (n->expansion != NULL && n->expansion->parent != NULL) {
        // This is a child node - use parent's snapshot and extract the bit
        GwNode *parent = n->expansion->parent;
        int bit_index = n->expansion->parentbit;
        
        if (n->nname) {
            fprintf(stderr, "BSEARCH: Child node %s using parent snapshot, bit=%d, key=%" GW_TIME_FORMAT "\n",
                    n->nname, bit_index, key);
        }
        
        // Get parent's snapshot
        GwNodeHistory *parent_history = gw_node_get_history_snapshot(parent);
        GwHistEnt **parent_harray;
        int parent_numhist;
        
        if (parent_history != NULL) {
            parent_harray = gw_node_history_get_harray(parent_history);
            parent_numhist = gw_node_history_get_numhist(parent_history);
        } else {
            parent_harray = parent->harray;
            parent_numhist = parent->numhist;
        }
        
        // Search in parent's harray for the time
        if (parent_harray != NULL && parent_numhist > 0) {
            if (bsearch(&key, parent_harray, parent_numhist, sizeof(GwHistEnt *), compar_histent)) {
                /* nothing, all side effects are in bsearch */
            }
        }
        
        // The result is in GLOBALS->max_compare_pos_bsearch_c_1
        // But we need to extract the bit value from the vector
        // For now, just return the parent's history entry
        // The calling code will need to extract the bit
        
        if (parent_history != NULL) {
            gw_node_history_unref(parent_history);
        }
        
        return (GLOBALS->max_compare_pos_bsearch_c_1);
    }
    
    // Regular node (not expanded) - use snapshot if available
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
