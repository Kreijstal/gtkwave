#include <gio/gio.h>
#include "gw-vcd-stream-loader.h"
#include "gw-vcd-file.h"
#include "gw-vcd-file-private.h"
#include "gw-util.h"
#include "gw-hash.h"
#include "vcd-keywords.h"
#include <stdio.h>
#include <errno.h>

#ifdef WAVE_USE_STRUCT_PACKING
#pragma pack(push)
#pragma pack(1)
#endif

// Copied from gw-vcd-loader.c
struct vcdsymbol
{
    struct vcdsymbol *root;
    struct vcdsymbol *chain;
    GwSymbol *sym_chain;

    struct vcdsymbol *next;
    char *name;
    char *id;
    char *value;
    GwNode **narray;

    unsigned int nid;
    int msi, lsi;
    int size;

    unsigned char vartype;
};

#ifdef WAVE_USE_STRUCT_PACKING
#pragma pack(pop)
#endif

struct _GwVcdStreamLoader
{
    GwLoader parent_instance;

    GByteArray *buffer;
    gsize buffer_pos;

    gboolean header_over;

    gboolean vlist_prepack;
    gint vlist_compression_level;
    GwVlist *time_vlist;
    unsigned int time_vlist_count;

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

    GwDumpFile *dump_file;
};


G_DEFINE_TYPE(GwVcdStreamLoader, gw_vcd_stream_loader, GW_TYPE_LOADER)

static GwDumpFile *gw_vcd_stream_loader_load(GwLoader *loader, const gchar *fname, GError **error)
{
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "GwVcdStreamLoader does not support loading from files");
    return NULL;
}

static void gw_vcd_stream_loader_class_init(GwVcdStreamLoaderClass *klass)
{
    GwLoaderClass *loader_class = GW_LOADER_CLASS(klass);
    loader_class->load = gw_vcd_stream_loader_load;
}

static void gw_vcd_stream_loader_init(GwVcdStreamLoader *self)
{
    self->buffer = g_byte_array_new();
    self->buffer_pos = 0;
    self->current_time = -1;
    self->start_time = -1;
    self->end_time = -1;
    self->time_scale = 1;
    self->time_dimension = 'n';
    self->T_MAX_STR = 1024;
    self->yytext = g_malloc(self->T_MAX_STR + 1);
    self->vcd_minid = G_MAXUINT;
    self->tree_builder = gw_tree_builder_new(VCD_HIERARCHY_DELIMITER);
    self->blackout_regions = gw_blackout_regions_new();
    self->vlist_compression_level = -1;
    self->sym_hash = g_new0(GwSymbol *, 16381);
    self->warning_filesize = 256;
}

GwLoader *gw_vcd_stream_loader_new(void)
{
    return g_object_new(GW_TYPE_VCD_STREAM_LOADER, NULL);
}

static inline signed char getch(GwVcdStreamLoader *self)
{
    if (self->buffer_pos < self->buffer->len) {
        return self->buffer->data[self->buffer_pos++];
    }
    return -2;
}

static int get_token(GwVcdStreamLoader *self)
{
    // This is a simplified version. A real implementation needs to handle rewinding.
    int ch;
    int i, len = 0;
    int is_string = 0;
    char *yyshadow;

    for (;;) {
        ch = getch(self);
        if (ch == -2) return T_NEED_MORE_DATA;
        if (ch < 0) return T_EOF;
        if (ch <= ' ') continue;
        break;
    }
    if (ch == '$') {
        self->yytext[len++] = ch;
        for (;;) {
            ch = getch(self);
            if (ch == -2) return T_NEED_MORE_DATA;
            if (ch < 0) return T_EOF;
            if (ch <= ' ') continue;
            break;
        }
    } else {
        is_string = 1;
    }

    for (self->yytext[len++] = ch;; self->yytext[len++] = ch) {
        if (len == self->T_MAX_STR) {
            self->T_MAX_STR *= 2;
            self->yytext = g_realloc(self->yytext, self->T_MAX_STR + 1);
        }
        ch = getch(self);
        if (ch <= ' ') break;
        if (ch == -2) return T_NEED_MORE_DATA;
    }
    self->yytext[len] = 0;
    self->yylen = len;

    if (is_string) return T_STRING;

    yyshadow = self->yytext;
    do {
        yyshadow++;
        for (i = 0; i < NUM_TOKENS; i++) {
            if (!strcmp(yyshadow, tokens[i])) return (i);
        }
    } while (*yyshadow == '$');
    return T_UNKNOWN_KEY;
}

static void vcd_parse(GwVcdStreamLoader *self, GError **error)
{
    g_assert(error == NULL || *error == NULL);

    while (*error == NULL) {
        int tok = get_token(self);
        switch (tok) {
	    case T_NEED_MORE_DATA:
		return;
            case T_COMMENT: sync_end(self); break;
            case T_DATE: sync_end(self); break;
            case T_VERSION: version_sync_end(self); break;
            case T_TIMEZERO: vcd_parse_timezero(self); break;
            case T_TIMESCALE: vcd_parse_timescale(self); break;
            case T_SCOPE: vcd_parse_scope(self); break;
            case T_UPSCOPE: vcd_parse_upscope(self); break;
            case T_VAR: vcd_parse_var(self); break;
            case T_ENDDEFINITIONS: vcd_parse_enddefinitions(self, error); break;
            case T_STRING: vcd_parse_string(self); break;
            case T_DUMPALL: case T_DUMPPORTSALL: break;
            case T_DUMPOFF: case T_DUMPPORTSOFF: gw_blackout_regions_add_dumpoff(self->blackout_regions, self->current_time); break;
            case T_DUMPON: case T_DUMPPORTSON: gw_blackout_regions_add_dumpon(self->blackout_regions, self->current_time); break;
            case T_DUMPVARS: case T_DUMPPORTS: if (self->current_time < 0) { self->start_time = self->current_time = self->end_time = 0; } break;
            case T_VCDCLOSE: sync_end(self); break;
            case T_END: break;
            case T_UNKNOWN_KEY: sync_end(self); break;
            case T_EOF: gw_blackout_regions_add_dumpon(self->blackout_regions, self->current_time); self->pv = NULL; if (self->prev_hier_uncompressed_name) { g_free(self->prev_hier_uncompressed_name); self->prev_hier_uncompressed_name = NULL; } return;
            default: break;
        }
    }
}

void gw_vcd_stream_loader_pump(GwVcdStreamLoader *self, const guint8 *buffer, gsize size, GError **error)
{
    g_return_if_fail(GW_IS_VCD_STREAM_LOADER(self));
    g_byte_array_append(self->buffer, buffer, size);
    vcd_parse(self, error);
    g_byte_array_remove_range(self->buffer, 0, self->buffer_pos);
    self->buffer_pos = 0;
}

void gw_vcd_stream_loader_eof(GwVcdStreamLoader *self, GError **error)
{
    g_return_if_fail(GW_IS_VCD_STREAM_LOADER(self));

    if (self->varsplit) { g_free(self->varsplit); self->varsplit = NULL; }
    gw_vlist_freeze(&self->time_vlist, self->vlist_compression_level);
    vlist_emit_finalize(self);

    if (self->symbols_sorted == NULL && self->symbols_indexed == NULL) {
        g_set_error(error, GW_DUMP_FILE_ERROR, GW_DUMP_FILE_ERROR_NO_SYMBOLS, "No symbols in VCD file..is it malformed?");
        return;
    }

    GwTime min_time = self->start_time * self->time_scale;
    GwTime max_time = self->end_time * self->time_scale;
    self->global_time_offset *= self->time_scale;

    if (min_time == max_time && max_time == GW_TIME_CONSTANT(-1)) {
        g_set_error(error, GW_DUMP_FILE_ERROR, GW_DUMP_FILE_ERROR_NO_TRANSITIONS, "No transitions in VCD file");
        return;
    }

    vcd_build_symbols(self);
    GwFacs *facs = vcd_sortfacs(self);
    self->tree_root = gw_tree_builder_build(self->tree_builder);
    GwTree *tree = vcd_build_tree(self, facs);
    vcd_cleanup(self);

    gw_blackout_regions_scale(self->blackout_regions, self->time_scale);

    GwTimeRange *time_range = gw_time_range_new(min_time, max_time);

    GwVcdFile *dump_file = g_object_new(GW_TYPE_VCD_FILE,
                                        "tree", tree,
                                        "facs", facs,
                                        "blackout-regions", self->blackout_regions,
                                        "time-scale", self->time_scale,
                                        "time-dimension", self->time_dimension,
                                        "time-range", time_range,
                                        "global-time-offset", self->global_time_offset,
                                        "has-escaped-names", self->has_escaped_names,
                                        NULL);

    dump_file->start_time = self->start_time;
    dump_file->end_time = self->end_time;
    dump_file->time_vlist = self->time_vlist;
    dump_file->is_prepacked = self->vlist_prepack;

    self->dump_file = GW_DUMP_FILE(dump_file);

    g_object_unref(tree);
    g_object_unref(time_range);
}

GwDumpFile *gw_vcd_stream_loader_get_dump_file(GwVcdStreamLoader *self)
{
    g_return_val_if_fail(GW_IS_VCD_STREAM_LOADER(self), NULL);
    return self->dump_file;
}
