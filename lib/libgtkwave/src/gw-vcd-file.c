#include "gw-vcd-file.h"
#include "gw-vcd-file-private.h"
#include "gw-vlist-reader.h"
#include "gw-node-history.h"
#include "gw-bit.h"
#include <stdio.h>

G_DEFINE_TYPE(GwVcdFile, gw_vcd_file, GW_TYPE_DUMP_FILE)

static void gw_vcd_file_import_trace(GwVcdFile *self, GwNode *np);
static void add_histent_scalar(GwVcdFile *self, GwNodeHistory *history, GwTime tim, GwBit bit, GwVarType vartype);
static void add_histent_vector(GwVcdFile *self, GwNodeHistory *history, GwTime tim, guint8 *vector, guint len);
static void add_histent_real(GwVcdFile *self, GwNodeHistory *history, GwTime tim, gdouble value);
static void add_histent_string(GwVcdFile *self, GwNodeHistory *history, GwTime tim, const char *str);
static void gw_vcd_file_import_trace_scalar(GwVcdFile *self, GwNodeHistory *history, GwNode *np, GwVlistReader *reader);
static void gw_vcd_file_import_trace_vector(GwVcdFile *self, GwNodeHistory *history, GwNode *np, GwVlistReader *reader, guint32 len);
static void gw_vcd_file_import_trace_real(GwVcdFile *self, GwNodeHistory *history, GwNode *np, GwVlistReader *reader);
static void gw_vcd_file_import_trace_string(GwVcdFile *self, GwNodeHistory *history, GwNode *np, GwVlistReader *reader);


static gboolean gw_vcd_file_import_traces(GwDumpFile *dump_file, GwNode **nodes, GError **error)
{
    GwVcdFile *self = GW_VCD_FILE(dump_file);
    (void)error;

    for (GwNode **iter = nodes; *iter != NULL; iter++) {
        GwNode *node = *iter;

        if (node->userdata != NULL) {
            gw_vcd_file_import_trace(self, node);
        }
    }

    return TRUE;
}

static void gw_vcd_file_dispose(GObject *object)
{
    GwVcdFile *self = GW_VCD_FILE(object);
    g_clear_object(&self->hist_ent_factory);
    G_OBJECT_CLASS(gw_vcd_file_parent_class)->dispose(object);
}

static void gw_vcd_file_class_init(GwVcdFileClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GwDumpFileClass *dump_file_class = GW_DUMP_FILE_CLASS(klass);
    object_class->dispose = gw_vcd_file_dispose;
    dump_file_class->import_traces = gw_vcd_file_import_traces;
}

static void gw_vcd_file_init(GwVcdFile *self)
{
    self->hist_ent_factory = gw_hist_ent_factory_new();
}

static void add_histent_string(GwVcdFile *self, GwNodeHistory *history, GwTime tim, const char *str)
{
    if (history->curr->time == tim) {
        g_free(history->curr->v.h_vector);
        history->curr->v.h_vector = g_strdup(str);
        if (!(history->curr->flags & GW_HIST_ENT_FLAG_GLITCH)) {
            history->curr->flags |= GW_HIST_ENT_FLAG_GLITCH;
        }
    } else {
        GwHistEnt *he = gw_hist_ent_factory_alloc(self->hist_ent_factory);
        he->flags = (GW_HIST_ENT_FLAG_STRING | GW_HIST_ENT_FLAG_REAL);
        he->time = tim;
        he->v.h_vector = g_strdup(str);
        gw_node_history_append_entry(history, he);
    }
}

static void add_histent_real(GwVcdFile *self, GwNodeHistory *history, GwTime tim, gdouble value)
{
    if ((history->curr->v.h_double != value) || (tim == self->start_time) || (self->preserve_glitches) || (self->preserve_glitches_real)) {
        if (history->curr->time == tim) {
            history->curr->v.h_double = value;
            if (!(history->curr->flags & GW_HIST_ENT_FLAG_GLITCH)) {
                history->curr->flags |= GW_HIST_ENT_FLAG_GLITCH;
            }
        } else {
            GwHistEnt *he = gw_hist_ent_factory_alloc(self->hist_ent_factory);
            he->flags = GW_HIST_ENT_FLAG_REAL;
            he->time = tim;
            he->v.h_double = value;
            gw_node_history_append_entry(history, he);
        }
    }
}

static void add_histent_vector(GwVcdFile *self, GwNodeHistory *history, GwTime tim, guint8 *vector, guint len)
{
    if ((history->curr->v.h_vector && vector && memcmp(history->curr->v.h_vector, vector, len) != 0) ||
        (tim == self->start_time) || (!history->curr->v.h_vector) || (self->preserve_glitches)) {
        if (history->curr->time == tim) {
            g_free(history->curr->v.h_vector);
            history->curr->v.h_vector = (char*)vector;
            if (!(history->curr->flags & GW_HIST_ENT_FLAG_GLITCH)) {
                history->curr->flags |= GW_HIST_ENT_FLAG_GLITCH;
            }
        } else {
            GwHistEnt *he = gw_hist_ent_factory_alloc(self->hist_ent_factory);
            he->time = tim;
            he->v.h_vector = (char*)vector;
            gw_node_history_append_entry(history, he);
        }
    } else {
        g_free(vector);
    }
}

static void add_histent_scalar(GwVcdFile *self, GwNodeHistory *history, GwTime tim, GwBit bit, GwVarType vartype)
{
    if ((history->curr->v.h_val != bit) || (tim == self->start_time) ||
        (vartype == GW_VAR_TYPE_VCD_EVENT) || (self->preserve_glitches)) {
        if (history->curr->time == tim) {
            history->curr->v.h_val = bit;
            if (!(history->curr->flags & GW_HIST_ENT_FLAG_GLITCH)) {
                history->curr->flags |= GW_HIST_ENT_FLAG_GLITCH;
            }
        } else {
            GwHistEnt *he = gw_hist_ent_factory_alloc(self->hist_ent_factory);
            he->time = tim;
            he->v.h_val = bit;
            if (history->curr->v.h_val == bit) {
                history->curr->flags |= GW_HIST_ENT_FLAG_GLITCH;
            }
            gw_node_history_append_entry(history, he);
        }
    }
}

static void gw_vcd_file_import_trace_scalar(GwVcdFile *self, GwNodeHistory *history, GwNode *np, GwVlistReader *reader)
{
    GwTime time_scale = gw_dump_file_get_time_scale(GW_DUMP_FILE(self));
    unsigned int time_idx = 0;
    static const GwBit EXTRA_VALUES[] = {GW_BIT_X, GW_BIT_Z, GW_BIT_H, GW_BIT_U, GW_BIT_W, GW_BIT_L, GW_BIT_DASH, GW_BIT_X};

    while (!gw_vlist_reader_is_done(reader)) {
        guint32 accum = gw_vlist_reader_read_uv32(reader);
        GwBit bit;
        guint delta;
        if (!(accum & 1)) {
            delta = accum >> 2;
            bit = accum & 2 ? GW_BIT_1 : GW_BIT_0;
        } else {
            delta = accum >> 4;
            guint index = (accum >> 1) & 7;
            bit = EXTRA_VALUES[index];
        }
        time_idx += delta;
        GwTime *curtime_pnt = gw_vlist_locate(self->time_vlist, time_idx ? time_idx - 1 : 0);
        if (!curtime_pnt) g_error("malformed bitwise signal data for '%s' after time_idx = %d", np->nname, time_idx - delta);
        GwTime t = *curtime_pnt * time_scale;
        add_histent_scalar(self, history, t, bit, np->vartype);
    }
    add_histent_scalar(self, history, GW_TIME_MAX - 1, GW_BIT_X, np->vartype);
    add_histent_scalar(self, history, GW_TIME_MAX, GW_BIT_Z, np->vartype);
}

static void gw_vcd_file_import_trace_vector(GwVcdFile *self, GwNodeHistory *history, GwNode *np, GwVlistReader *reader, guint32 len)
{
    GwTime time_scale = gw_dump_file_get_time_scale(GW_DUMP_FILE(self));
    unsigned int time_idx = 0;
    guint8 *sbuf = g_malloc(len + 1);

    while (!gw_vlist_reader_is_done(reader)) {
        guint delta = gw_vlist_reader_read_uv32(reader);
        time_idx += delta;
        GwTime *curtime_pnt = gw_vlist_locate(self->time_vlist, time_idx ? time_idx - 1 : 0);
        if (!curtime_pnt) g_error("malformed 'b' signal data for '%s' after time_idx = %d", np->nname, time_idx - delta);
        GwTime t = *curtime_pnt * time_scale;

        guint32 dst_len = 0;
        for (;;) {
            gint c = gw_vlist_reader_next(reader);
            if (c < 0 || (c >> 4) == GW_BIT_MASK) break;
            if (dst_len == len) { if (len != 1) memmove(sbuf, sbuf + 1, dst_len - 1); dst_len--; }
            sbuf[dst_len++] = c >> 4;
            if ((c & GW_BIT_MASK) == GW_BIT_MASK) break;
            if (dst_len == len) { if (len != 1) memmove(sbuf, sbuf + 1, dst_len - 1); dst_len--; }
            sbuf[dst_len++] = c & GW_BIT_MASK;
        }

        if (len == 1) {
            add_histent_scalar(self, history, t, sbuf[0], np->vartype);
        } else {
            guint8 *vector = g_malloc(len + 1);
            if (dst_len < len) {
                GwBit extend = (sbuf[0] == GW_BIT_1) ? GW_BIT_0 : sbuf[0];
                memset(vector, extend, len - dst_len);
                memcpy(vector + (len - dst_len), sbuf, dst_len);
            } else {
                memcpy(vector, sbuf, len);
            }
            vector[len] = 0;
            add_histent_vector(self, history, t, vector, len);
        }
    }

    if (len == 1) {
        add_histent_scalar(self, history, GW_TIME_MAX - 1, GW_BIT_X, np->vartype);
        add_histent_scalar(self, history, GW_TIME_MAX, GW_BIT_Z, np->vartype);
    } else {
        guint8 *x = g_malloc0(len); memset(x, GW_BIT_X, len);
        guint8 *z = g_malloc0(len); memset(z, GW_BIT_Z, len);
        add_histent_vector(self, history, GW_TIME_MAX - 1, x, len);
        add_histent_vector(self, history, GW_TIME_MAX, z, len);
    }
    g_free(sbuf);
}

static void gw_vcd_file_import_trace_real(GwVcdFile *self, GwNodeHistory *history, GwNode *np, GwVlistReader *reader)
{
    GwTime time_scale = gw_dump_file_get_time_scale(GW_DUMP_FILE(self));
    unsigned int time_idx = 0;

    while (!gw_vlist_reader_is_done(reader)) {
        unsigned int delta = gw_vlist_reader_read_uv32(reader);
        time_idx += delta;
        GwTime *curtime_pnt = gw_vlist_locate(self->time_vlist, time_idx ? time_idx - 1 : 0);
        if (!curtime_pnt) g_error("malformed 'r' signal data for '%s' after time_idx = %d\n", np->nname, time_idx - delta);
        GwTime t = *curtime_pnt * time_scale;
        const gchar *str = gw_vlist_reader_read_string(reader);
        gdouble value = 0.0;
        sscanf(str, "%lg", &value);
        add_histent_real(self, history, t, value);
    }
    add_histent_real(self, history, GW_TIME_MAX - 1, 1.0);
    add_histent_real(self, history, GW_TIME_MAX, 0.0);
}

static void gw_vcd_file_import_trace_string(GwVcdFile *self, GwNodeHistory *history, GwNode *np, GwVlistReader *reader)
{
    GwTime time_scale = gw_dump_file_get_time_scale(GW_DUMP_FILE(self));
    unsigned int time_idx = 0;

    while (!gw_vlist_reader_is_done(reader)) {
        unsigned int delta = gw_vlist_reader_read_uv32(reader);
        time_idx += delta;
        GwTime *curtime_pnt = gw_vlist_locate(self->time_vlist, time_idx ? time_idx - 1 : 0);
        if (!curtime_pnt) g_error("malformed 's' signal data for '%s' after time_idx = %d", np->nname, time_idx - delta);
        GwTime t = *curtime_pnt * time_scale;
        const gchar *str = gw_vlist_reader_read_string(reader);
        add_histent_string(self, history, t, str);
    }
    add_histent_string(self, history, GW_TIME_MAX - 1, "UNDEF");
    add_histent_string(self, history, GW_TIME_MAX, "");
}

static void gw_vcd_file_import_trace(GwVcdFile *self, GwNode *np)
{
    guint32 len = 1;
    guint32 vlist_type;

    GwVlist *vlist = np->userdata;
    if (vlist == NULL) {
        return;
    }
    np->userdata = NULL; /* vlist is consumed */
    gw_vlist_uncompress(&vlist);

    GwVlistReader *reader = gw_vlist_reader_new(vlist, self->is_prepacked);

    if (gw_vlist_reader_is_done(reader)) {
        len = 1;
        vlist_type = '!';
    } else {
        vlist_type = gw_vlist_reader_read_uv32(reader);
        switch (vlist_type) {
            case '0': {
                len = 1;
                gint c = gw_vlist_reader_next(reader);
                if (c < 0) g_error("Internal error file '%s' line %d", __FILE__, __LINE__);
                break;
            }
            case 'B':
            case 'R':
            case 'S': {
                gint c = gw_vlist_reader_next(reader);
                if (c < 0) g_error("Internal error file '%s' line %d", __FILE__, __LINE__);
                len = gw_vlist_reader_read_uv32(reader);
                break;
            }
            default:
                g_error("Unsupported vlist type '%c'", vlist_type);
                break;
        }
    }

    GwNodeHistory *old_history = gw_node_get_history_snapshot(np);

    if (vlist_type == '!')
    {
        if(old_history && old_history->curr) {
            GwNode *n2 = (GwNode *)old_history->curr;
             if ((n2) && (n2 != np)) {
                gw_vcd_file_import_trace(self, n2);
                g_clear_object(&reader);

                GwNodeHistory *history_to_share = gw_node_get_history_snapshot(n2);
                GwNodeHistory *history_to_free = gw_node_publish_new_history(np, history_to_share);
                if(history_to_free) gw_node_history_unref(history_to_free);
                gw_node_history_unref(history_to_share);
                gw_node_history_unref(old_history);
                return;
            }
        }
        if(old_history) gw_node_history_unref(old_history);
        g_error("Error in decompressing vlist for '%s'", np->nname);
    }

    GwNodeHistory *new_history = gw_node_history_copy(old_history);
    gw_node_history_unref(old_history);

    if (vlist_type == '0') {
        gw_vcd_file_import_trace_scalar(self, new_history, np, reader);
    } else if (vlist_type == 'B') {
        gw_vcd_file_import_trace_vector(self, new_history, np, reader, len);
    } else if (vlist_type == 'R') {
        gw_vcd_file_import_trace_real(self, new_history, np, reader);
    } else if (vlist_type == 'S') {
        gw_vcd_file_import_trace_string(self, new_history, np, reader);
    }

    gw_node_history_regenerate_harray(new_history);
    GwNodeHistory *old_history_to_free = gw_node_publish_new_history(np, new_history);
    if(old_history_to_free) gw_node_history_unref(old_history_to_free);
    gw_node_history_unref(new_history);

    g_clear_object(&reader);
}