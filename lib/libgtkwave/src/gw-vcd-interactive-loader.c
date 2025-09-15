#include "gw-vcd-interactive-loader.h"
#include "gw-project.h"
#include <stdio.h>
#include "gw-tree.h"
#include "gw-symbol.h"
#include <glib.h>
#include <string.h>

struct _GwVcdInteractiveLoader {
    GObject parent_instance;
    
    GMutex queue_mutex;
    GCond data_cond;
    GQueue *data_queue;
    gboolean processing;
    gboolean complete;
    guint idle_source_id;
    GThread *processing_thread;
    
    // VCD parser state
    GString *current_line;
    gboolean header_parsed;
    GHashTable *symbols_by_id;
    GQueue *pending_symbols;
    GwTime current_time;
    
    // Parser state for VCD interactive loading
    gpointer parser_state;
    gpointer vcd_file_state;
};

G_DEFINE_TYPE_WITH_CODE(GwVcdInteractiveLoader, gw_vcd_interactive_loader, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GW_TYPE_INTERACTIVE_LOADER, NULL))

static gboolean gw_vcd_interactive_loader_process_data(GwInteractiveLoader *loader, 
                                                      const gchar *data, gsize length) {
    GwVcdInteractiveLoader *self = GW_VCD_INTERACTIVE_LOADER(loader);
    
    g_mutex_lock(&self->queue_mutex);
    g_queue_push_tail(self->data_queue, g_strndup(data, length));
    g_cond_signal(&self->data_cond);
    g_mutex_unlock(&self->queue_mutex);
    
    return TRUE;
}

static gboolean gw_vcd_interactive_loader_is_complete(GwInteractiveLoader *loader) {
    GwVcdInteractiveLoader *self = GW_VCD_INTERACTIVE_LOADER(loader);
    return self->complete;
}

static void gw_vcd_interactive_loader_kick_processing(GwInteractiveLoader *loader) {
    GwVcdInteractiveLoader *self = GW_VCD_INTERACTIVE_LOADER(loader);
    
    g_mutex_lock(&self->queue_mutex);
    g_cond_signal(&self->data_cond);
    g_mutex_unlock(&self->queue_mutex);
}

static void gw_vcd_interactive_loader_cleanup(GwInteractiveLoader *loader) {
    GwVcdInteractiveLoader *self = GW_VCD_INTERACTIVE_LOADER(loader);
    
    // Signal completion and wait for thread
    g_mutex_lock(&self->queue_mutex);
    self->complete = TRUE;
    g_cond_signal(&self->data_cond);
    g_mutex_unlock(&self->queue_mutex);
    
    // Wait for processing thread to complete
    if (self->processing_thread) {
        g_thread_join(self->processing_thread);
        self->processing_thread = NULL;
    }
    
    // Clean up remaining resources...
}

// VCD parser state management
static void vcd_parser_init(GwVcdInteractiveLoader *self)
{
    if (self->parser_state) return;
    
    self->current_line = g_string_new(NULL);
    self->header_parsed = FALSE;
    self->symbols_by_id = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    self->pending_symbols = g_queue_new();
    self->current_time = 0;
    

}

static void vcd_parser_cleanup(gpointer parser_state)
{
    GwVcdInteractiveLoader *self = parser_state;
    
    if (self->current_line) {
        g_string_free(self->current_line, TRUE);
        self->current_line = NULL;
    }
    
    if (self->symbols_by_id) {
        g_hash_table_destroy(self->symbols_by_id);
        self->symbols_by_id = NULL;
    }
    
    if (self->pending_symbols) {
        g_queue_free_full(self->pending_symbols, g_free);
        self->pending_symbols = NULL;
    }
}

// Process VCD chunk in GTK thread context
static void process_vcd_chunk(GwVcdInteractiveLoader *self, const gchar *data) {
    if (!self->parser_state) {
        vcd_parser_init(self);
        self->parser_state = self; // Use self as parser state
    }
    

    
    const gchar *ptr = data;
    while (*ptr) {
        if (*ptr == '\n' || *ptr == '\r') {
            // Process complete line
            if (self->current_line->len > 0) {
                gchar *line = g_string_free(g_string_new(self->current_line->str), FALSE);
                
                // Simple VCD parsing - handle time changes and value changes
                if (line[0] == '#') {
                    // Time change
                    self->current_time = g_ascii_strtoll(line + 1, NULL, 10);
                    g_debug("Time changed to: %" G_GINT64_FORMAT, self->current_time);

                } else if (line[0] == '$') {
                    // Command - for now just skip
                    if (g_str_has_prefix(line, "$end")) {
                        // End of command
                    }
                } else if (strchr("01xzXZ", line[0]) && line[1] != ' ') {
                    // Scalar value change
                    gchar *id = g_strdup(line + 1);
                    gchar value = line[0];
                    g_debug("Scalar change: %s = %c", id, value);

                    g_free(id);
                } else if (line[0] == 'b' || line[0] == 'B') {
                    // Binary value change
                    gchar *space = strchr(line, ' ');
                    if (space) {
                        *space = '\0';
                        gchar *id = g_strdup(space + 1);
                        gchar *value = g_strdup(line + 1);
                        g_debug("Binary change: %s = %s", id, value);

                        g_free(id);
                        g_free(value);
                    }
                }
                
                g_free(line);
                g_string_truncate(self->current_line, 0);
            }
            
            // Skip newline characters
            while (*ptr == '\n' || *ptr == '\r') ptr++;
            continue;
        }
        
        g_string_append_c(self->current_line, *ptr);
        ptr++;
    }
}

// GTK idle callback for processing
static gboolean process_idle_callback(gpointer user_data) {
    GwVcdInteractiveLoader *self = user_data;
    
    g_mutex_lock(&self->queue_mutex);
    if (g_queue_is_empty(self->data_queue)) {
        g_mutex_unlock(&self->queue_mutex);
        return G_SOURCE_CONTINUE; // Keep callback active
    }
    
    // Process a chunk of data in the GTK thread
    guint processed = 0;
    while (!g_queue_is_empty(self->data_queue) && processed < 1000) {
        gchar *data = g_queue_pop_head(self->data_queue);
        // Process VCD data in GTK thread
        process_vcd_chunk(self, data);
        g_free(data);
        processed++;
    }
    
    g_mutex_unlock(&self->queue_mutex);
    
    // Continue if more data or not complete
    return !(g_queue_is_empty(self->data_queue) && self->complete) ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

// Start GTK processing
static void gw_vcd_interactive_loader_start_gtk_processing(GwVcdInteractiveLoader *self) {
    g_mutex_lock(&self->queue_mutex);
    if (self->idle_source_id == 0) {
        self->idle_source_id = g_idle_add(process_idle_callback, self);
    }
    g_mutex_unlock(&self->queue_mutex);
}

// Processing thread function
static gpointer process_data_thread(gpointer user_data) {
    GwVcdInteractiveLoader *self = user_data;
    
    while (!self->complete) {
        g_mutex_lock(&self->queue_mutex);
        while (g_queue_is_empty(self->data_queue) && !self->complete) {
            g_cond_wait(&self->data_cond, &self->queue_mutex);
        }
        
        // Start GTK processing if we have data
        if (!g_queue_is_empty(self->data_queue)) {
            gw_vcd_interactive_loader_start_gtk_processing(self);
        }
        
        g_mutex_unlock(&self->queue_mutex);
        
        // Sleep briefly to allow GTK processing
        g_usleep(10000); // 10ms
    }
    
    return NULL;
}

static void gw_vcd_interactive_loader_init(GwVcdInteractiveLoader *self) {
    g_mutex_init(&self->queue_mutex);
    g_cond_init(&self->data_cond);
    self->data_queue = g_queue_new();
    self->processing = FALSE;
    self->complete = FALSE;
    self->parser_state = NULL;
    self->vcd_file_state = NULL;
    
    // Start processing thread
    self->processing_thread = g_thread_new("vcd-processor", process_data_thread, self);
}

static void gw_vcd_interactive_loader_finalize(GObject *object) {
    GwVcdInteractiveLoader *self = GW_VCD_INTERACTIVE_LOADER(object);
    
    g_mutex_lock(&self->queue_mutex);
    self->complete = TRUE;
    g_cond_signal(&self->data_cond);
    
    // Remove idle source if it exists
    if (self->idle_source_id != 0) {
        g_source_remove(self->idle_source_id);
        self->idle_source_id = 0;
    }
    g_mutex_unlock(&self->queue_mutex);
    
    // Clean up queue
    while (!g_queue_is_empty(self->data_queue)) {
        gchar *data = g_queue_pop_head(self->data_queue);
        g_free(data);
    }
    g_queue_free(self->data_queue);
    
    g_mutex_clear(&self->queue_mutex);
    g_cond_clear(&self->data_cond);
    
    // Clean up parser state properly
    if (self->parser_state != NULL) {
        vcd_parser_cleanup(self->parser_state);
        self->parser_state = NULL;
    }
    
    // Clean up vcd_file state
    if (self->vcd_file_state != NULL) {
        g_free(self->vcd_file_state);
        self->vcd_file_state = NULL;
    }

    // Clean up processing thread
    if (self->processing_thread != NULL) {
        g_thread_join(self->processing_thread);
        self->processing_thread = NULL;
    }
    
    G_OBJECT_CLASS(gw_vcd_interactive_loader_parent_class)->finalize(object);
}

static void gw_vcd_interactive_loader_class_init(GwVcdInteractiveLoaderClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = gw_vcd_interactive_loader_finalize;
}

static void gw_vcd_interactive_loader_interface_init(GwInteractiveLoaderInterface *iface) {
    iface->process_data = gw_vcd_interactive_loader_process_data;
    iface->is_complete = gw_vcd_interactive_loader_is_complete;
    iface->kick_processing = gw_vcd_interactive_loader_kick_processing;
    iface->cleanup = gw_vcd_interactive_loader_cleanup;
}

GwVcdInteractiveLoader *gw_vcd_interactive_loader_new(void) {
    return g_object_new(GW_TYPE_VCD_INTERACTIVE_LOADER, NULL);
}