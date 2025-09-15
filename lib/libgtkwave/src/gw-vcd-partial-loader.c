#include "gw-vcd-partial-loader.h"
#include "gw-vcd-loader-private.h"
#include "gw-vcd-file.h"
#include "gw-vcd-file-private.h"
#include "gw-util.h"
#include "gw-hash.h"
#include "gw-vlist-reader.h"
#include "gw-vlist.h"
#include "vcd-keywords.h"
/* Forward declaration of vcdsymbol struct */
struct vcdsymbol {
    struct vcdsymbol *root, *chain;
    void *sym_chain; /* GwSymbol * sym_chain; */
    struct vcdsymbol *next;
    char *name;
    char *id;
    char *value;
    void **narray; /* GwNode **narray; */
    void **tr_array; /* GwHistEnt **tr_array; */
    void **app_array; /* GwHistEnt **app_array; */
    unsigned int nid;
    int msi, lsi;
    int size;
    unsigned char vartype;
};
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define VCD_BSIZ 32768 /* size of getch() emulation buffer--this val should be ok */

#ifdef __MINGW32__
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#define WAVE_PARTIAL_VCD_RING_BUFFER_SIZE (16 * 1024 * 1024)

/* now the recoded "extra" values... */
#define RCV_X (1 | (0 << 1))
#define RCV_Z (1 | (1 << 1))
#define RCV_H (1 | (2 << 1))
#define RCV_U (1 | (3 << 1))
#define RCV_W (1 | (4 << 1))
#define RCV_L (1 | (5 << 1))
#define RCV_D (1 | (6 << 1))

struct _GwVcdPartialLoader
{
    GwVcdLoader parent_instance;

    /* Partial VCD specific members */
    guint shared_memory_id;
    gpointer shared_memory_ptr;
    gpointer consume_ptr;
    gboolean streaming_enabled;
    gboolean is_partial_vcd;

    /* Callback for real-time updates */
    GwVcdPartialUpdateCallback update_callback;
    gpointer update_callback_data;

    /* Partial VCD state */
    GwTime partial_start_time;
    GwTime partial_end_time;
    guint consume_countdown;
};

G_DEFINE_TYPE(GwVcdPartialLoader, gw_vcd_partial_loader, GW_TYPE_VCD_LOADER)

enum
{
    PROP_SHARED_MEMORY_ID = 1,
    PROP_STREAMING_ENABLED,
    N_PROPERTIES,
};

static GParamSpec *properties[N_PROPERTIES];

/* Forward declarations */
static void vcd_partial_parse(GwVcdPartialLoader *self, GError **error);
static void vcd_partial_build_symbols(GwVcdPartialLoader *self);
static void vcd_partial_regen_harray(GwVcdPartialLoader *self, GwNode *nd);
static void vcd_partial_regen_trace_mark(GwTrace *t, gboolean mandclear);
static void vcd_partial_finalize_vlists(GwVcdLoader *self);
static void vcd_partial_regen_trace_sweep(GwVcdPartialLoader *self, GwTrace *t);
static void vcd_partial_regen_node_expansion(GwTrace *t);
static int vcd_partial_getch_fetch(GwVcdLoader *loader);

static char vlist_value_to_char(guint32 rcv);

/* Trace context integration - these would be implemented with application integration */
static GwTrace *get_traces_from_context(void)
{
    /* TODO: Implement proper integration with main application's trace management */
    /* This would return the current list of traces from the application context */
    g_warning("Trace context integration not yet implemented - returning NULL");
    return NULL;
}

static GwTrace *get_buffer_traces_from_context(void)
{
    /* TODO: Implement proper integration with main application's trace management */
    /* This would return the current list of buffer traces from the application context */
    g_warning("Buffer trace context integration not yet implemented - returning NULL");
    return NULL;
}

/* Shared memory functions */
static gboolean attach_shared_memory(GwVcdPartialLoader *self, GError **error)
{

#ifdef __MINGW32__
    HANDLE hMapFile;
    char mapName[257];

    sprintf(mapName, "shmidcat%d", self->shared_memory_id);
    hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, mapName);
    if (hMapFile == NULL) {
        g_set_error(error, GW_DUMP_FILE_ERROR, GW_DUMP_FILE_ERROR_UNKNOWN,
                   "Could not attach shared memory map name '%s'", mapName);
        return FALSE;
    }

    self->shared_memory_ptr = self->consume_ptr = 
        MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, WAVE_PARTIAL_VCD_RING_BUFFER_SIZE);
    if (self->shared_memory_ptr == NULL) {
        g_set_error(error, GW_DUMP_FILE_ERROR, GW_DUMP_FILE_ERROR_UNKNOWN,
                   "Could not map view of file '%s'", mapName);
        CloseHandle(hMapFile);
        return FALSE;
    }
#else
    errno = 0;

    self->shared_memory_ptr = self->consume_ptr = 
        shmat(self->shared_memory_id, NULL, 0);

    if (errno) {

        g_set_error(error, GW_DUMP_FILE_ERROR, GW_DUMP_FILE_ERROR_UNKNOWN,
                   "Could not attach shared memory ID %08x", self->shared_memory_id);
        return FALSE;
    }

#endif

    return TRUE;
}

static void detach_shared_memory(GwVcdPartialLoader *self)
{
    if (self->shared_memory_ptr) {
#ifdef __MINGW32__
        UnmapViewOfFile(self->shared_memory_ptr);
#else
        shmdt(self->shared_memory_ptr);
#endif
        self->shared_memory_ptr = self->consume_ptr = NULL;
    }
}

/* Memory access functions for partial VCD */
static unsigned int get_8(GwVcdPartialLoader *self, gpointer p)
{
    if (p >= ((gpointer)self->shared_memory_ptr + WAVE_PARTIAL_VCD_RING_BUFFER_SIZE)) {
        p = (gpointer)((guintptr)p - WAVE_PARTIAL_VCD_RING_BUFFER_SIZE);
    }
    return (unsigned int)((unsigned char)*(char *)p);
}

static unsigned int get_32(GwVcdPartialLoader *self, gpointer p)
{
    unsigned int val = 0;
    val |= (get_8(self, p) << 24);
    val |= (get_8(self, p + 1) << 16);
    val |= (get_8(self, p + 2) << 8);
    val |= (get_8(self, p + 3));
    return val;
}

/* VCD buffer allocation functions */
static void getch_alloc(GwVcdLoader *self)
{
    self->vcdbuf = g_malloc0(VCD_BSIZ + 1);
    self->vst = self->vcdbuf;
    self->vend = self->vcdbuf;
}

static void getch_free(GwVcdLoader *self)
{
    g_free(self->vcdbuf);
    self->vcdbuf = NULL;
    self->vst = NULL;
    self->vend = NULL;
}

static int consume(GwVcdPartialLoader *self)
{
    int len;




    
    // Check if consume_ptr is within valid bounds
    if (self->consume_ptr < self->shared_memory_ptr || 
        self->consume_ptr >= (gpointer)((guintptr)self->shared_memory_ptr + WAVE_PARTIAL_VCD_RING_BUFFER_SIZE)) {
        fprintf(stderr, "ERROR: consume_ptr %p is outside shared memory bounds [%p, %p]\n",
               self->consume_ptr, self->shared_memory_ptr, 
               (gpointer)((guintptr)self->shared_memory_ptr + WAVE_PARTIAL_VCD_RING_BUFFER_SIZE));
        return 0;
    }
    
    self->consume_countdown--;
    if (!self->consume_countdown) {
        self->consume_countdown = 100000;

        return 0;
    }


    if ((len = *(char *)self->consume_ptr)) {
        int i;


        len = get_32(self, self->consume_ptr + 1);

        for (i = 0; i < len; i++) {
            gpointer src_ptr = self->consume_ptr + i + 5;

            unsigned char byte_val = get_8(self, src_ptr);

            if (GW_VCD_LOADER(self) == NULL) {
                fprintf(stderr, "ERROR: VCD loader is NULL!\n");
                return 0;
            }
            if (GW_VCD_LOADER(self)->vcdbuf == NULL) {
                fprintf(stderr, "ERROR: vcdbuf is NULL!\n");
                return 0;
            }
            GW_VCD_LOADER(self)->vcdbuf[i] = byte_val;
        }
        GW_VCD_LOADER(self)->vcdbuf[i] = 0;




        *(char *)self->consume_ptr = 0;


        self->consume_ptr = (gpointer)((guintptr)self->consume_ptr + i + 5);
        if (self->consume_ptr >= 
            ((gpointer)self->shared_memory_ptr + WAVE_PARTIAL_VCD_RING_BUFFER_SIZE)) {

            self->consume_ptr = (gpointer)((guintptr)self->consume_ptr - WAVE_PARTIAL_VCD_RING_BUFFER_SIZE);
        }


        return len;
    }


    return 0;
}

/* Override getch_fetch for partial VCD */
static int vcd_partial_getch_fetch(GwVcdLoader *loader)
{
    size_t rd;


    GwVcdPartialLoader *self = GW_VCD_PARTIAL_LOADER(loader);
    errno = 0;
    loader->vcdbyteno += (loader->vend - loader->vcdbuf);
    rd = consume(self);
    loader->vend = (loader->vst = loader->vcdbuf) + rd;

    if ((!rd) || (errno)) {

        return (-1);
    }


    return ((int)(*loader->vst));
}

/* Partial VCD parsing */
static void vcd_partial_parse(GwVcdPartialLoader *self, GError **error)
{
    if (self->consume_ptr) {
    }
    GwVcdLoader *vcd_loader = GW_VCD_LOADER(self);
    
    /* Override getch_fetch to use shared memory */
    vcd_loader->getch_fetch_override = vcd_partial_getch_fetch;
        
    while (*error == NULL && *(char *)self->consume_ptr) {
        /* Use the parent's parsing state machine but with our getch_fetch */
        vcd_parse(vcd_loader, error);
            
        /* Update time ranges */
        self->partial_end_time = vcd_loader->end_time;
        if (self->partial_start_time == GW_TIME_CONSTANT(-1)) {
            self->partial_start_time = vcd_loader->start_time;
        }
            
        /* Notify update callback */
        if (self->update_callback) {
            self->update_callback(self, vcd_loader->current_time, self->update_callback_data);
        }
            
        /* Check if we should continue parsing */
        if (!*(char *)self->consume_ptr) {
            break;
        }
    }
        
    /* Restore original getch_fetch */
    vcd_loader->getch_fetch_override = NULL;
}

/* Partial VCD symbol building */
static void vcd_partial_build_symbols(GwVcdPartialLoader *self)
{
    /* For partial VCD, we use the parent's symbol building logic */
    vcd_build_symbols(GW_VCD_LOADER(self));
}

/* Minimal vlist finalization for partial loader */
static unsigned int vlist_emit_finalize(GwVcdLoader *self)
{
    struct vcdsymbol *v;
    int cnt = 0;

    /* Walk through all symbols and finalize their vlist writers */
    v = self->vcdsymroot;
    while (v) {
        /* Check if narray exists and has at least one element */
        if (v->narray != NULL && v->narray[0] != NULL) {
            GwNode *n = v->narray[0];
            
            /* Check if vlist writer exists before accessing it */
            if (n->mv.mvlfac_vlist_writer != NULL) {
                /* Finalize the vlist writer and create the actual vlist */
                GwVlistWriter *writer = n->mv.mvlfac_vlist_writer;
                n->mv.mvlfac_vlist = gw_vlist_writer_finish(writer);
                g_object_unref(writer);
                n->mv.mvlfac_vlist_writer = NULL;
            }
        }

        v = v->next;
        cnt++;
    }

    return cnt;
}



/* Vlist decompression functions */
static char vlist_value_to_char(guint32 rcv)
{
    switch (rcv & 0xF) {
        case RCV_X: return 'x';
        case RCV_Z: return 'z';
        case RCV_H: return 'h';
        case RCV_U: return 'u';
        case RCV_W: return 'w';
        case RCV_L: return 'l';
        case RCV_D: return '-';
        default: return (rcv & 2) ? '1' : '0';
    }
}

/* Regeneration functions adapted for vlist system */
static void vcd_partial_regen_harray(GwVcdPartialLoader *self, GwNode *nd)
{
    /* For vlist-based implementation, we need to regenerate the decompressed 
       history array from the vlist data when needed for display */
    if (!nd->mv.mvlfac_vlist) {
        return;
    }
    
    /* Decompress vlist and create traditional history array */
    GwVlistReader *reader = gw_vlist_reader_new(nd->mv.mvlfac_vlist, FALSE);
    if (!reader) {
        return;
    }
    
    /* Read the header to determine the value type */
    guint32 header = gw_vlist_reader_read_uv32(reader);
    char value_type = (char)(header & 0xFF);
    
    /* Get time scale from the parent loader */
    GwVcdLoader *vcd_loader = GW_VCD_LOADER(self);
    GwTime time_scale = vcd_loader->time_scale;
    
    GwTime current_time = 0;
    GList *history_entries = NULL;
    
    /* Process the vlist data based on value type */
    if (value_type == '0') {
        /* Single bit values */
        /* guint32 vartype = */ gw_vlist_reader_read_uv32(reader);
        guint32 initial_value = gw_vlist_reader_read_uv32(reader);
        
        /* Create initial history entry */
        GwHistEnt *he = g_new0(GwHistEnt, 1);
        he->time = current_time;
        he->v.h_val = vlist_value_to_char(initial_value);
        history_entries = g_list_append(history_entries, he);
        
        /* Process all value changes */
        while (!gw_vlist_reader_is_done(reader)) {
            guint32 rcv = gw_vlist_reader_read_uv32(reader);
            guint32 time_delta = rcv >> 4;
            char value = vlist_value_to_char(rcv);
            
            current_time += time_delta;
            
            GwHistEnt *he = g_new0(GwHistEnt, 1);
            he->time = current_time * time_scale;
            he->v.h_val = value;
            history_entries = g_list_append(history_entries, he);
        }
    } else if (value_type == 'B' || value_type == 'R' || value_type == 'S') {
        /* Binary, real, or string values - more complex handling needed */
        /* TODO: Implement full vector value decompression */
        g_warning("Vector value decompression not yet implemented for partial VCD");
    }
    
    /* Free existing harray if it exists */
    if (nd->harray) {
        g_free(nd->harray);
        nd->harray = NULL;
    }
    
    /* Convert linked list to array */
    nd->numhist = g_list_length(history_entries);
    if (nd->numhist > 0) {
        nd->harray = g_new0(GwHistEnt *, nd->numhist);
        
        GList *iter = history_entries;
        for (int i = 0; i < nd->numhist; i++) {
            nd->harray[i] = (GwHistEnt *)iter->data;
            iter = g_list_next(iter);
        }
    }
    
    /* Free the linked list (but not the elements - they're in the array now) */
    g_list_free(history_entries);
    g_object_unref(reader);
}

static void vcd_partial_regen_node_expansion(GwTrace *t)
{
    if (!t->vector) {
        if (t->n.nd && t->n.nd->expansion) {
            /* Handle node expansion regeneration for partial VCD */
            t->interactive_vector_needs_regeneration = 1;
        }
    }
}

static void vcd_partial_regen_trace_mark(GwTrace *t, gboolean mandclear)
{
    if (t->vector) {
        GwBitVector *b = t->n.vec;
        GwBits *bts = b->bits;
        int i;

        if (1) {
            for (i = 0; i < bts->nnbits; i++) {
                if (!bts->nodes[i]->expansion) {
                    /* Mark for regeneration if vlist needs decompression */
                    if (bts->nodes[i]->mv.mvlfac_vlist && mandclear) {
                        /* TODO: Handle vlist decompression state */
                        t->interactive_vector_needs_regeneration = 1;
                    }
                } else {
                    t->interactive_vector_needs_regeneration = 1;
                }
            }
        }

        for (i = 0; i < bts->nnbits; i++) {
            if (!bts->nodes[i]->mv.mvlfac_vlist) {
                t->interactive_vector_needs_regeneration = 1;
                return;
            }
        }
    } else {
        if (t->n.nd) /* comment and blank traces don't have a valid node */
            if ((t->n.nd->mv.mvlfac_vlist) && (mandclear)) {
                /* Mark for vlist regeneration */
                t->interactive_vector_needs_regeneration = 1;
            }
    }
}

static void vcd_partial_regen_trace_sweep(GwVcdPartialLoader *self, GwTrace *t)
{
    if (t->interactive_vector_needs_regeneration) {
        if (!t->vector) {
            vcd_partial_regen_harray(self, t->n.nd);
        } else {
            /* TODO: Implement vector trace regeneration with vlist */
            /* This would regenerate vector traces from compressed vlist data */
            GwBitVector *b = t->n.vec;
            /* GwBits *bts = b->bits; */
            
            /* For now, just mark that we need to handle vector regeneration */
            g_warning("Vector trace regeneration not yet implemented for partial VCD");
        }
        t->interactive_vector_needs_regeneration = 0;
    }
}

/* Public API implementation */
void gw_vcd_partial_loader_kick(GwVcdPartialLoader *self)
{
    g_return_if_fail(GW_IS_VCD_PARTIAL_LOADER(self));
    
    if (self->streaming_enabled && self->is_partial_vcd) {
#ifdef __MINGW32__
        Sleep(10);
#else
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000000 / 100;
        select(0, NULL, NULL, NULL, &tv);
#endif

        GError *error = NULL;
        vcd_partial_parse(self, &error);
        
        if (error) {
            g_warning("Partial VCD parsing error: %s", error->message);
            g_error_free(error);
        }
    }
}

void gw_vcd_partial_loader_mark_and_sweep(GwVcdPartialLoader *self, gboolean mandclear)
{
    g_return_if_fail(GW_IS_VCD_PARTIAL_LOADER(self));
    
    if (!self->is_partial_vcd) {
        return;
    }
    
    /* Get the current trace context - this would need to be integrated with the main application */
    GwTrace *t = get_traces_from_context();
    
    /* Iterate through all traces and mark them for regeneration */
    while (t) {
        if (!t->vector)
            vcd_partial_regen_trace_mark(t, mandclear);
        t = t->t_next;
    }
    
    /* Iterate through buffer traces */
    t = get_buffer_traces_from_context();
    while (t) {
        if (!t->vector)
            vcd_partial_regen_trace_mark(t, mandclear);
        t = t->t_next;
    }
    
    /* Sweep through and regenerate marked traces */
    t = get_traces_from_context();
    while (t) {
        if (!t->vector)
            vcd_partial_regen_trace_sweep(self, t);
        t = t->t_next;
    }
    
    /* Sweep through buffer traces */
    t = get_buffer_traces_from_context();
    while (t) {
        if (!t->vector)
            vcd_partial_regen_trace_sweep(self, t);
        t = t->t_next;
    }
    
    /* Handle node expansions */
    t = get_traces_from_context();
    while (t) {
        vcd_partial_regen_node_expansion(t);
        t = t->t_next;
    }
    
    /* Handle buffer trace expansions */
    t = get_buffer_traces_from_context();
    while (t) {
        vcd_partial_regen_node_expansion(t);
        t = t->t_next;
    }
    
    /* Handle vector traces */
    t = get_traces_from_context();
    while (t) {
        if (t->vector)
            vcd_partial_regen_trace_mark(t, mandclear);
        t = t->t_next;
    }
    
    /* Handle buffer vector traces */
    t = get_buffer_traces_from_context();
    while (t) {
        if (t->vector)
            vcd_partial_regen_trace_mark(t, mandclear);
        t = t->t_next;
    }
    
    /* Sweep vector traces */
    t = get_traces_from_context();
    while (t) {
        if (t->vector)
            vcd_partial_regen_trace_sweep(self, t);
        t = t->t_next;
    }
    
    /* Sweep buffer vector traces */
    t = get_buffer_traces_from_context();
    while (t) {
        if (t->vector)
            vcd_partial_regen_trace_sweep(self, t);
        t = t->t_next;
    }
    
    /* Reset minmax validation for floating point traces */
    t = get_traces_from_context();
    while (t) {
        if (t->minmax_valid)
            t->minmax_valid = 0;
        t = t->t_next;
    }
    
    /* Reset buffer trace minmax validation */
    t = get_buffer_traces_from_context();
    while (t) {
        if (t->minmax_valid)
            t->minmax_valid = 0;
        t = t->t_next;
    }
}

/* Property handling */
static void gw_vcd_partial_loader_set_property(GObject *object, guint property_id,
                                              const GValue *value, GParamSpec *pspec)
{
    GwVcdPartialLoader *self = GW_VCD_PARTIAL_LOADER(object);

    switch (property_id) {
        case PROP_SHARED_MEMORY_ID:
            self->shared_memory_id = g_value_get_uint(value);
            break;
        case PROP_STREAMING_ENABLED:
            self->streaming_enabled = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void gw_vcd_partial_loader_get_property(GObject *object, guint property_id,
                                              GValue *value, GParamSpec *pspec)
{
    GwVcdPartialLoader *self = GW_VCD_PARTIAL_LOADER(object);

    switch (property_id) {
        case PROP_SHARED_MEMORY_ID:
            g_value_set_uint(value, self->shared_memory_id);
            break;
        case PROP_STREAMING_ENABLED:
            g_value_set_boolean(value, self->streaming_enabled);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

/* Loader implementation */
GwDumpFile *gw_vcd_partial_loader_load(GwLoader *loader, const gchar *fname, GError **error)
{
    g_return_val_if_fail(fname != NULL, NULL);
    g_return_val_if_fail(error == NULL || *error == NULL, NULL);

    GwVcdPartialLoader *self = GW_VCD_PARTIAL_LOADER(loader);



    /* Check if this is a partial VCD (shared memory ID) */
    if (!strcmp(fname, "-vcd")) {
        self->is_partial_vcd = TRUE;

        /* Shared memory ID should already be set via property, don't overwrite it */
        if (self->shared_memory_id == 0) {

            /* Fallback: try to parse from filename if not set */
            guint shmidu = ~0U;
            if (!sscanf(fname, "%x", &shmidu)) {
                shmidu = ~0U;
            }
            self->shared_memory_id = shmidu;

        } else {

            /* Shared memory ID already set by adapter, use that value */
            /* No need to parse from filename */
        }
        
        /* Allocate VCD buffer like parent class does */
        getch_alloc(GW_VCD_LOADER(self));

        
        /* Initialize time_vlist like parent class does */
        GW_VCD_LOADER(self)->time_vlist = gw_vlist_create(sizeof(GwTime));

    } else {
        /* Regular VCD file - delegate to parent */
        return GW_LOADER_CLASS(gw_vcd_partial_loader_parent_class)->load(loader, fname, error);
    }


    if (!attach_shared_memory(self, error)) {

        getch_free(GW_VCD_LOADER(self));
        return NULL;
    }


    /* Initialize partial VCD state */
    self->consume_countdown = 100000;
    self->partial_start_time = GW_TIME_CONSTANT(-1);
    self->partial_end_time = GW_TIME_CONSTANT(-1);


    /* Parse initial data */
    GError *internal_error = NULL;
    vcd_partial_parse(self, &internal_error);

    
    if (internal_error) {

        g_propagate_error(error, internal_error);
        detach_shared_memory(self);
        getch_free(GW_VCD_LOADER(self));
        if (GW_VCD_LOADER(self)->time_vlist) {
            gw_vlist_destroy(GW_VCD_LOADER(self)->time_vlist);
            GW_VCD_LOADER(self)->time_vlist = NULL;
        }
        return NULL;
    } else {

    }



    /* Finalize vlists before building symbols */
    vlist_emit_finalize(GW_VCD_LOADER(self));
    
    /* Build symbols and finalize */
    vcd_partial_build_symbols(self);
    
    GwFacs *facs = vcd_sortfacs(GW_VCD_LOADER(self));
    GwTree *tree = vcd_build_tree(GW_VCD_LOADER(self), facs);



    
    GwTimeRange *time_range = gw_time_range_new(
        self->partial_start_time * GW_VCD_LOADER(self)->time_scale,
        self->partial_end_time * GW_VCD_LOADER(self)->time_scale);

    return g_object_new(GW_TYPE_VCD_FILE,
                       "tree", tree,
                       "facs", facs,
                       "blackout-regions", GW_VCD_LOADER(self)->blackout_regions,
                       "time-scale", GW_VCD_LOADER(self)->time_scale,
                       "time-dimension", GW_VCD_LOADER(self)->time_dimension,
                       "time-range", time_range,
                       NULL);
}

static void gw_vcd_partial_loader_finalize(GObject *object)
{
    GwVcdPartialLoader *self = GW_VCD_PARTIAL_LOADER(object);

    detach_shared_memory(self);

    G_OBJECT_CLASS(gw_vcd_partial_loader_parent_class)->finalize(object);
}

static void gw_vcd_partial_loader_class_init(GwVcdPartialLoaderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GwLoaderClass *loader_class = GW_LOADER_CLASS(klass);

    object_class->finalize = gw_vcd_partial_loader_finalize;
    object_class->set_property = gw_vcd_partial_loader_set_property;
    object_class->get_property = gw_vcd_partial_loader_get_property;

    loader_class->load = gw_vcd_partial_loader_load;

    properties[PROP_SHARED_MEMORY_ID] =
        g_param_spec_uint("shared-memory-id",
                         "Shared Memory ID",
                         "Shared memory ID for partial VCD streaming",
                         0, G_MAXUINT, 0,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    properties[PROP_STREAMING_ENABLED] =
        g_param_spec_boolean("streaming-enabled",
                            "Streaming Enabled",
                            "Whether partial VCD streaming is enabled",
                            TRUE,
                            G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);
}

static void gw_vcd_partial_loader_init(GwVcdPartialLoader *self)
{
    self->shared_memory_id = 0;
    self->shared_memory_ptr = NULL;
    self->consume_ptr = NULL;
    self->streaming_enabled = TRUE;
    self->is_partial_vcd = FALSE;
    self->update_callback = NULL;
    self->update_callback_data = NULL;
    self->partial_start_time = GW_TIME_CONSTANT(-1);
    self->partial_end_time = GW_TIME_CONSTANT(-1);
    self->consume_countdown = 100000;
}

/* Public constructor */
GwLoader *gw_vcd_partial_loader_new(void)
{
    return g_object_new(GW_TYPE_VCD_PARTIAL_LOADER, NULL);
}

/* Public API methods */
void gw_vcd_partial_loader_set_shared_memory_id(GwVcdPartialLoader *self, guint shmid)
{
    g_return_if_fail(GW_IS_VCD_PARTIAL_LOADER(self));
    self->shared_memory_id = shmid;
}

guint gw_vcd_partial_loader_get_shared_memory_id(GwVcdPartialLoader *self)
{
    g_return_val_if_fail(GW_IS_VCD_PARTIAL_LOADER(self), 0);
    return self->shared_memory_id;
}

void gw_vcd_partial_loader_set_streaming_enabled(GwVcdPartialLoader *self, gboolean enabled)
{
    g_return_if_fail(GW_IS_VCD_PARTIAL_LOADER(self));
    self->streaming_enabled = enabled;
}

gboolean gw_vcd_partial_loader_is_streaming_enabled(GwVcdPartialLoader *self)
{
    g_return_val_if_fail(GW_IS_VCD_PARTIAL_LOADER(self), FALSE);
    return self->streaming_enabled;
}

void gw_vcd_partial_loader_set_update_callback(GwVcdPartialLoader *self,
                                              GwVcdPartialUpdateCallback callback,
                                              gpointer user_data)
{
    g_return_if_fail(GW_IS_VCD_PARTIAL_LOADER(self));
    self->update_callback = callback;
    self->update_callback_data = user_data;
}

GwVcdPartialUpdateCallback gw_vcd_partial_loader_get_update_callback(GwVcdPartialLoader *self)
{
    g_return_val_if_fail(GW_IS_VCD_PARTIAL_LOADER(self), NULL);
    return self->update_callback;
}

gpointer gw_vcd_partial_loader_get_update_callback_data(GwVcdPartialLoader *self)
{
    g_return_val_if_fail(GW_IS_VCD_PARTIAL_LOADER(self), NULL);
    return self->update_callback_data;
}