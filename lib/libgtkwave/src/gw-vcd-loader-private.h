/*
 * Copyright (c) 2024 GTKWave Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "gw-vcd-loader.h"
#include <stdio.h>

G_BEGIN_DECLS

// Define a function pointer type for getch overrides
typedef int (*GwVcdGetchFetchFunc)(GwVcdLoader *self);

// Expose the internal structure of GwVcdLoader
struct _GwVcdLoader
{
    GwLoader parent_instance;

    FILE *vcd_handle;
    gboolean is_compressed;
    off_t vcd_fsiz;

    gboolean header_over;

    gboolean vlist_prepack;
    gint vlist_compression_level;
    GwVlist *time_vlist;
    unsigned int time_vlist_count;

    off_t vcdbyteno;
    char *vcdbuf;
    char *vst;
    char *vend;

    int error_count;
    gboolean err;

    GwTime current_time;

    struct vcdsymbol *pv;
    struct vcdsymbol *rootv;

    int T_MAX_STR;
    char *yytext;
    int yylen;

    struct vcdsymbol *vcdsymroot;
    struct vcdsymbol *vcdsymcurr;

    int numsyms;
    struct vcdsymbol **symbols_sorted;
    struct vcdsymbol **symbols_indexed;

    guint vcd_minid;
    guint vcd_maxid;
    guint vcd_hash_max;
    gboolean vcd_hash_kill;
    gint hash_cache;
    GwSymbol **sym_hash;

    char *varsplit;
    char *vsplitcurr;
    int var_prevch;

    gboolean already_backtracked;

    GSList *sym_chain;

    GwBlackoutRegions *blackout_regions;

    GwTime time_scale;
    GwTimeDimension time_dimension;
    GwTime start_time;
    GwTime end_time;
    GwTime global_time_offset;

    GwTreeNode *tree_root;

    guint numfacs;
    gchar *prev_hier_uncompressed_name;

    GwTreeNode *terminals_chain;
    GwTreeBuilder *tree_builder;

    char *module_tree;
    int module_len_tree;

    gboolean has_escaped_names;
    guint warning_filesize;

    // Add this new member for our override mechanism
    GwVcdGetchFetchFunc getch_fetch_override;
    gpointer getch_fetch_override_data;
};

// Forward-declare the internal parsing functions we need to call
void vcd_parse(GwVcdLoader *self, GError **error);
void vcd_build_symbols(GwVcdLoader *self);
GwFacs *vcd_sortfacs(GwVcdLoader *self);
GwTree *vcd_build_tree(GwVcdLoader *self, GwFacs *facs);

// Forward-declare buffer management functions
void getch_alloc(GwVcdLoader *self);
void getch_free(GwVcdLoader *self);


G_END_DECLS