#include "gw-vlist-reader.h"
#include "gw-vlist-writer.h"
#include "gw-vlist.h"
#include "gw-vlist-packer.h"
#include "gw-bit.h"
#include <zlib.h>

struct _GwVlistReader
{
    GObject parent_instance;

    GwVlist *vlist;
    guint8 *depacked;
    gboolean prepacked;

    GwVlistWriter *live_writer;

    guint position;
    guint size;

    GString *string_buffer;
};

G_DEFINE_TYPE(GwVlistReader, gw_vlist_reader, G_TYPE_OBJECT)

enum
{
    PROP_VLIST = 1,
    PROP_PREPACKED,
    PROP_LIVE_WRITER,
    N_PROPERTIES,
};

static GParamSpec *properties[N_PROPERTIES];

static void gw_vlist_reader_finalize(GObject *object)
{
    GwVlistReader *self = GW_VLIST_READER(object);

    g_clear_pointer(&self->vlist, gw_vlist_destroy);
    g_clear_pointer(&self->depacked, gw_vlist_packer_decompress_destroy);
    g_clear_object(&self->live_writer);
    g_string_free(self->string_buffer, TRUE);

    G_OBJECT_CLASS(gw_vlist_reader_parent_class)->finalize(object);
}

static void gw_vlist_reader_constructed(GObject *object)
{
    GwVlistReader *self = GW_VLIST_READER(object);

    G_OBJECT_CLASS(gw_vlist_reader_parent_class)->constructed(object);

    if (self->live_writer) {
        // In live mode, size is dynamic. We don't depack.
        self->size = 0; // Will be updated on the fly
    } else if (self->prepacked) {
        self->depacked = gw_vlist_packer_decompress(self->vlist, &self->size);
        g_clear_pointer(&self->vlist, gw_vlist_destroy);
    } else {
        self->size = gw_vlist_size(self->vlist);
    }
}

static void gw_vlist_reader_set_property(GObject *object,
                                         guint property_id,
                                         const GValue *value,
                                         GParamSpec *pspec)
{
    GwVlistReader *self = GW_VLIST_READER(object);

    switch (property_id) {
        case PROP_VLIST:
            self->vlist = g_value_get_pointer(value);
            break;

        case PROP_PREPACKED:
            self->prepacked = g_value_get_boolean(value);
            break;

        case PROP_LIVE_WRITER:
            self->live_writer = g_value_get_object(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void gw_vlist_reader_class_init(GwVlistReaderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = gw_vlist_reader_finalize;
    object_class->constructed = gw_vlist_reader_constructed;
    object_class->set_property = gw_vlist_reader_set_property;

    properties[PROP_VLIST] =
        g_param_spec_pointer("vlist", NULL, NULL,
                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PREPACKED] =
        g_param_spec_boolean("prepacked", NULL, NULL, FALSE,
                              G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LIVE_WRITER] =
        g_param_spec_object("live-writer", NULL, NULL, GW_TYPE_VLIST_WRITER,
                            G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);


    g_object_class_install_properties(object_class, N_PROPERTIES, properties);
}

static void gw_vlist_reader_init(GwVlistReader *self)
{
    self->string_buffer = g_string_new(NULL);
}

GwVlistReader *gw_vlist_reader_new(GwVlist *vlist, gboolean prepacked)
{
    return g_object_new(GW_TYPE_VLIST_READER,
                        "vlist", vlist,
                        "prepacked", prepacked,
                        NULL);
}

GwVlistReader *gw_vlist_reader_new_from_writer(GwVlistWriter *writer)
{
    return g_object_new(GW_TYPE_VLIST_READER,
                        "live-writer", writer,
                        NULL);
}

gint gw_vlist_reader_next(GwVlistReader *self)
{
    g_return_val_if_fail(GW_IS_VLIST_READER(self), -1);

    if (self->live_writer) {
        GwVlist *live_vlist = gw_vlist_writer_get_live_vlist(self->live_writer);
        if (!live_vlist) {
            return -2; // No data yet
        }

        guint live_size = gw_vlist_size(live_vlist);
        if (self->position >= live_size) {
            return -2; // Need more data
        }

        guint8 value = *(guint8 *)gw_vlist_locate(live_vlist, self->position);
        self->position++;
        return value;

    } else {
        if (self->position >= self->size) {
            return -1; // EOF
        }

        guint8 value = 0;
        if (self->depacked != NULL) {
            value = self->depacked[self->position];
        } else {
            value = *(guint8 *)gw_vlist_locate(self->vlist, self->position);
        }

        self->position++;
        return value;
    }
}

guint32 gw_vlist_reader_read_uv32(GwVlistReader *self)
{
    g_return_val_if_fail(GW_IS_VLIST_READER(self), 0);

    guint8 arr[5];
    gint arr_pos = 0;

    gint c = 0;
    do {
        c = gw_vlist_reader_next(self);
        if (c < 0) {
            break;
        }
        g_assert_cmpint(arr_pos, <, 5);
        arr[arr_pos++] = c & 0x7F;
    } while ((c & 0x80) == 0);

    g_assert_cmpint(arr_pos, >, 0);

    guint32 accum = 0;
    for (--arr_pos; arr_pos >= 0; arr_pos--) {
        guint8 c = arr[arr_pos];
        accum <<= 7;
        accum |= c;
    }

    return accum;
}

const gchar *gw_vlist_reader_read_string(GwVlistReader *self)
{
    g_return_val_if_fail(GW_IS_VLIST_READER(self), NULL);

    g_string_truncate(self->string_buffer, 0);

    while (TRUE) {
        gint c = gw_vlist_reader_next(self);
        if (c <= 0) {
            break;
        }
        g_string_append_c(self->string_buffer, c);
    };

    return self->string_buffer->str;
}

gboolean gw_vlist_reader_is_done(GwVlistReader *self)
{
    g_return_val_if_fail(GW_IS_VLIST_READER(self), TRUE);

    return self->position >= self->size;
}