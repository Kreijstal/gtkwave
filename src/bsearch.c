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

    // Special handling for expanded child nodes: rebuild history from parent dynamically
    // This ensures child nodes stay synchronized with streaming updates to the parent
    if (n->expansion && n->expansion->parent) {
        GwNode *parent = n->expansion->parent;
        int bit_index = n->expansion->parentbit;
        
        // Free existing child history if any (except head)
        if (n->harray) {
            g_free(n->harray);
            n->harray = NULL;
        }
        GwHistEnt *h_next = n->head.next;
        while (h_next) {
            GwHistEnt *h_temp = h_next->next;
            g_free(h_next);
            h_next = h_temp;
        }
        n->head.next = NULL;
        
        // Ensure parent has its harray built
        if (parent->harray == NULL) {
            // Recursively call bsearch_node on parent to build its harray
            bsearch_node(parent, key);
        }
        
        if (parent->harray != NULL && parent->numhist > 0) {
            // Build child node's history from parent's vector data
            GwHistEnt *child_curr = &(n->head);
            n->head.time = -1;
            n->head.v.h_val = GW_BIT_X;
            n->head.next = NULL;
            child_curr = &(n->head);
            
            GwBit last_val = GW_BIT_X;
            int child_histcount = 1; // Start with head
            
            for (int i = 0; i < parent->numhist; i++) {
                GwHistEnt *parent_h = parent->harray[i];
                
                // Skip special time markers and entries without vector data
                if (parent_h->time < 0 || parent_h->time >= GW_TIME_MAX - 1 || 
                    !parent_h->v.h_vector) {
                    continue;
                }
                
                // Extract bit value from parent's vector
                unsigned char raw_val = parent_h->v.h_vector[bit_index];
                GwBit val;
                switch (raw_val) {
                    case '0': val = GW_BIT_0; break;
                    case '1': val = GW_BIT_1; break;
                    case 'x':
                    case 'X': val = GW_BIT_X; break;
                    case 'z':
                    case 'Z': val = GW_BIT_Z; break;
                    case 'h':
                    case 'H': val = GW_BIT_H; break;
                    case 'l':
                    case 'L': val = GW_BIT_L; break;
                    case 'u':
                    case 'U': val = GW_BIT_U; break;
                    case 'w':
                    case 'W': val = GW_BIT_W; break;
                    case '-': val = GW_BIT_DASH; break;
                    default: val = (GwBit)raw_val; break;
                }
                
                // Only create new history entry if value changed
                if (val != last_val) {
                    GwHistEnt *new_h = g_new0(GwHistEnt, 1);
                    new_h->time = parent_h->time;
                    new_h->v.h_val = val;
                    new_h->next = NULL;
                    
                    child_curr->next = new_h;
                    child_curr = new_h;
                    last_val = val;
                    child_histcount++;
                }
            }
            
            n->numhist = child_histcount;
            n->curr = child_curr;
            
            // Build harray for the child
            GwHistEnt **harray = g_new(GwHistEnt *, child_histcount);
            n->harray = harray;
            
            GwHistEnt *histpnt = &(n->head);
            for (int i = 0; i < child_histcount; i++) {
                *harray = histpnt;
                harray++;
                histpnt = histpnt->next;
            }
        } else {
            // Parent has no history, just use the head
            n->numhist = 1;
            GwHistEnt **harray = g_new(GwHistEnt *, 1);
            n->harray = harray;
            harray[0] = &(n->head);
        }
    }
    // Rebuild harray if it's NULL (e.g., after streaming VCD data added new history entries)
    // This branch is for non-expanded nodes only
    else if (n->harray == NULL) {
        GwHistEnt *histpnt = &(n->head);
        int histcount = 0;

        while (histpnt) {
            histcount++;
            histpnt = histpnt->next;
        }

        n->numhist = histcount;

        GwHistEnt **harray = g_new(GwHistEnt *, histcount);
        n->harray = harray;

        histpnt = &(n->head);
        for (int i = 0; i < histcount; i++) {
            *harray = histpnt;
            harray++;
            histpnt = histpnt->next;
        }
    }

    if (bsearch(&key, n->harray, n->numhist, sizeof(GwHistEnt *), compar_histent)) {
        /* nothing, all side effects are in bsearch */
    }

    if ((!GLOBALS->max_compare_pos_bsearch_c_1) ||
        (GLOBALS->max_compare_time_bsearch_c_1 < GW_TIME_CONSTANT(0))) {
        GLOBALS->max_compare_pos_bsearch_c_1 = n->harray[1]; /* aix bsearch fix */
        GLOBALS->max_compare_index = &(n->harray[1]);
    }

    while (GLOBALS->max_compare_pos_bsearch_c_1->next) /* non-RoSync dumper deglitching fix */
    {
        if (GLOBALS->max_compare_pos_bsearch_c_1->time !=
            GLOBALS->max_compare_pos_bsearch_c_1->next->time)
            break;
        GLOBALS->max_compare_pos_bsearch_c_1 = GLOBALS->max_compare_pos_bsearch_c_1->next;
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
