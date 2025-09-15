#pragma once

#include <stdio.h>

#include "gw-vcd-loader.h"

G_BEGIN_DECLS

typedef int (*GwVcdGetchFetchFunc)(GwVcdLoader *self);

/* Forward declarations for internal parsing functions */
void vcd_parse(GwVcdLoader *self, GError **error);
void vcd_build_symbols(GwVcdLoader *self);
GwFacs *vcd_sortfacs(GwVcdLoader *self);
GwTree *vcd_build_tree(GwVcdLoader *self, GwFacs *facs);

struct _GwVcdLoader
{
    GwLoader parent_instance;

    /* Public members */
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

    /* Private extension for partial VCD support */
    GwVcdGetchFetchFunc getch_fetch_override;
};

G_END_DECLS