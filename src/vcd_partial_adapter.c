#include "vcd_partial_adapter.h"
#include "gw-vcd-partial-loader.h"
#include "gw-shared-memory.h"
#include "globals.h"
#include "wavewindow.h"
#include "analyzer.h"
#include <unistd.h>
#include <string.h>

#define WAVE_PARTIAL_VCD_RING_BUFFER_SIZE (1024 * 1024)

static GwVcdPartialLoader *the_loader = NULL;
static guint the_timer_id = 0;
static GwSharedMemory *shm = NULL;
static guint8 *shm_data = NULL;
static gsize consume_offset = 0;

// Helper functions for reading from shared memory
static guint8 get_8(guint8 *base, gssize offset)
{
    (void)base; // Parameter is unused in this context
    return shm_data[offset % WAVE_PARTIAL_VCD_RING_BUFFER_SIZE];
}

static guint32 get_32(guint8 *base, gssize offset)
{
    guint32 val = 0;
    val |= (get_8(base, offset)     << 24);
    val |= (get_8(base, offset + 1) << 16);
    val |= (get_8(base, offset + 2) << 8);
    val |= (get_8(base, offset + 3));
    return val;
}

// Timer callback that reads from shared memory and feeds the partial loader
// Timer callback to import signals after UI initialization
static gboolean import_signals_timeout_callback(gpointer user_data)
{
    fprintf(stderr, "DEBUG: Importing signals into UI...\n");
    fprintf(stderr, "DEBUG: Dump file pointer: %p\n", GLOBALS->dump_file);
    if (GLOBALS->dump_file) {
        GwFacs *facs = gw_dump_file_get_facs(GLOBALS->dump_file);
        fprintf(stderr, "DEBUG: Facs pointer: %p\n", facs);
        if (facs) {
            fprintf(stderr, "DEBUG: Number of signals: %u\n", gw_facs_get_length(facs));
        }
    }
    analyzer_import_all_signals();
    fprintf(stderr, "DEBUG: Signal import completed\n");
    return G_SOURCE_REMOVE; // Run only once
}

static gboolean kick_timeout_callback(gpointer user_data)
{
    if (!the_loader || !shm_data) {
        the_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    gboolean data_processed = FALSE;
    
    // Process all available data in the shared memory
    while (get_8(shm_data, consume_offset) != 0) {
        guint32 len = get_32(shm_data, consume_offset + 1);
        
        if (len > 0 && len < 32768) { // Reasonable size limit
            gchar *data_chunk = g_malloc(len + 1);
            
            for (guint32 i = 0; i < len; i++) {
                data_chunk[i] = get_8(shm_data, consume_offset + 5 + i);
            }
            data_chunk[len] = '\0';
            
            // Feed the data to the partial loader
            GError *error = NULL;
            gboolean success = gw_vcd_partial_loader_feed(the_loader, data_chunk, len, &error);
            
            if (!success) {
                fprintf(stderr, "Partial loader feed error: %s\n", error ? error->message : "Unknown error");
                if (error) g_error_free(error);
                g_free(data_chunk);
                the_timer_id = 0;
                return G_SOURCE_REMOVE;
            }
            
            g_free(data_chunk);
            data_processed = TRUE;
        }
        
        // Mark as consumed and move to next block
        shm_data[consume_offset % WAVE_PARTIAL_VCD_RING_BUFFER_SIZE] = 0;
        consume_offset += (5 + len);
    }
    
    // If we processed data, update the UI
    if (data_processed) {
        // Update the global dump file reference
        GLOBALS->dump_file = gw_vcd_partial_loader_get_dump_file(the_loader);
        
        if (GLOBALS->dump_file) {
            // Update the time range
            GwTimeRange *range = gw_dump_file_get_time_range(GLOBALS->dump_file);
            if (range) {
                GLOBALS->tims.last = gw_time_range_get_end(range);
            }
            
            // Redraw the UI
            fix_wavehadj();
            update_time_box();
            redraw_signals_and_waves();
        }
    }
    
    return G_SOURCE_CONTINUE;
}

// Helper function to synchronously parse VCD header
static gboolean parse_vcd_header_synchronously(void)
{
    fprintf(stderr, "DEBUG: Synchronously parsing VCD header...\n");
    
    // Block and read until the header is fully parsed
    while (!gw_vcd_partial_loader_is_header_parsed(the_loader)) {
        // Check if there's a block ready in the ring buffer
        if (get_8(shm_data, consume_offset) != 0) {
            guint32 len = get_32(shm_data, consume_offset + 1);
            
            if (len > 0 && len < 32768) { // Reasonable size limit
                gchar *header_chunk = g_malloc(len + 1);
                
                for (guint32 i = 0; i < len; i++) {
                    header_chunk[i] = get_8(shm_data, consume_offset + 5 + i);
                }
                header_chunk[len] = '\0';
                
                // Feed the header chunk to the loader
                GError *error = NULL;
                gboolean success = gw_vcd_partial_loader_feed(the_loader, header_chunk, len, &error);
                
                if (!success) {
                    fprintf(stderr, "Header parse error: %s\n", error ? error->message : "Unknown error");
                    if (error) g_error_free(error);
                    g_free(header_chunk);
                    return FALSE;
                }
                
                g_free(header_chunk);
                
                // Mark as consumed and move to next block
                shm_data[consume_offset % WAVE_PARTIAL_VCD_RING_BUFFER_SIZE] = 0;
                consume_offset += (5 + len);
            }
        } else {
            // No data yet, sleep briefly to avoid busy-waiting
            g_usleep(10000); // 10ms
        }
    }
    
    fprintf(stderr, "DEBUG: VCD header parsed successfully\n");
    return TRUE;
}

GwDumpFile *vcd_partial_adapter_main(const gchar *shm_id)
{
    fprintf(stderr, "DEBUG: vcd_partial_adapter_main called with SHM ID: %s\n", shm_id);
    vcd_partial_adapter_cleanup(); // Clean any previous instance

    // Open the shared memory segment
    GError *error = NULL;
    fprintf(stderr, "DEBUG: Attempting to open SHM segment '%s'\n", shm_id);
    shm = gw_shared_memory_open(shm_id, &error);
    if (!shm) {
        fprintf(stderr, "Error: Could not open SHM segment '%s': %s\n", 
                shm_id, error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return NULL;
    }
    fprintf(stderr, "DEBUG: Successfully opened SHM segment\n");
    
    shm_data = (guint8 *)gw_shared_memory_get_data(shm);
    consume_offset = 0;
    fprintf(stderr, "DEBUG: SHM data pointer: %p, size: %zu\n", shm_data, gw_shared_memory_get_size(shm));
    
    // Create the partial loader
    the_loader = gw_vcd_partial_loader_new();
    fprintf(stderr, "DEBUG: Partial loader created: %p\n", the_loader);
    if (!the_loader) {
        fprintf(stderr, "Error: Failed to create VCD partial loader\n");
        gw_shared_memory_free(shm);
        shm = NULL;
        shm_data = NULL;
        return NULL;
    }
    
    // SYNCHRONOUSLY PARSE THE VCD HEADER BEFORE RETURNING
    if (!parse_vcd_header_synchronously()) {
        fprintf(stderr, "Error: Failed to parse VCD header\n");
        vcd_partial_adapter_cleanup();
        return NULL;
    }
    
    // Start the timer to periodically check for new data (value changes)
    the_timer_id = g_timeout_add(100, kick_timeout_callback, NULL); // Check every 100ms
    fprintf(stderr, "DEBUG: Timer started with ID: %u\n", the_timer_id);
    
    // Return the now-valid dump file with complete signal hierarchy
    GwDumpFile *dump_file = gw_vcd_partial_loader_get_dump_file(the_loader);
    fprintf(stderr, "DEBUG: Dump file after header parse: %p\n", dump_file);
    
    // Schedule signal import after UI is fully initialized
    fprintf(stderr, "DEBUG: Scheduling signal import after UI initialization...\n");
    g_timeout_add(500, import_signals_timeout_callback, NULL); // Increased to 500ms
    
    return dump_file;
}

void vcd_partial_adapter_cleanup(void)
{
    if (the_timer_id > 0) {
        g_source_remove(the_timer_id);
        the_timer_id = 0;
    }
    
    if (shm) {
        gw_shared_memory_free(shm);
        shm = NULL;
        shm_data = NULL;
    }
    
    if (the_loader) {
        g_object_unref(the_loader);
        the_loader = NULL;
    }
    
    consume_offset = 0;
}