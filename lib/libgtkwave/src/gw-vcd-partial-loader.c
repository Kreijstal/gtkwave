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
 * THE AAVTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "gw-vcd-partial-loader.h"
#include "gw-vcd-partial-loader-private.h" // For accessing internal structure
#include "gw-vcd-loader-private.h" // We need access to the parent's guts
#include "gw-vcd-file.h"
#include "gw-vcd-file-private.h" // For accessing internal GwVcdFile members
#include "gw-vcd-loader.h" // For accessing the loader structure

// VCD buffer size from gw-vcd-loader.h
#define VCD_BSIZ 32768

#include "gw-facs.h"
#include "gw-tree.h"
#include "gw-time-range.h"
#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include "gw-time.h"
#include "gw-bit.h"

// Forward declaration of GwTimeRange internal structure for direct access
typedef struct _GwTimeRangeInternal {
    GObject parent_instance;
    GwTime start;
    GwTime end;
} GwTimeRangeInternal;

// Forward declarations for VCD loader internal functions
struct vcdsymbol;



#define RING_BUFFER_SIZE (1024 * 1024)



G_DEFINE_TYPE(GwVcdPartialLoader, gw_vcd_partial_loader, GW_TYPE_VCD_LOADER)

// --- Ring Buffer Access Functions ---
static guint8 get_8(guint8 *base, gssize offset)
{
    return base[offset % RING_BUFFER_SIZE];
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

static void put_8(guint8 *base, gssize offset, guint8 value)
{
    base[offset % RING_BUFFER_SIZE] = value;
}

static int vcd_partial_getch_fetch(GwVcdLoader *loader);

static void gw_vcd_partial_loader_init(GwVcdPartialLoader *self)
{
    self->shm = NULL;
    self->shm_data = NULL;
    self->consume_offset = 0;
    self->header_parsed = FALSE;
}

static void gw_vcd_partial_loader_finalize(GObject *object)
{
    GwVcdPartialLoader *self = GW_VCD_PARTIAL_LOADER(object);
    gw_vcd_partial_loader_cleanup(self);
    
    G_OBJECT_CLASS(gw_vcd_partial_loader_parent_class)->finalize(object);
}

static void gw_vcd_partial_loader_class_init(GwVcdPartialLoaderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = gw_vcd_partial_loader_finalize;
}

GwVcdPartialLoader *gw_vcd_partial_loader_new(void)
{
    return g_object_new(GW_TYPE_VCD_PARTIAL_LOADER, NULL);
}

// --- Implementation of the getch override ---
static int vcd_partial_getch_fetch(GwVcdLoader *loader)
{
    GwVcdPartialLoader *self = GW_VCD_PARTIAL_LOADER(loader->getch_fetch_override_data);
    size_t rd = 0;
    gboolean data_processed = FALSE;

    if (!self->shm_data) {
        g_printerr("No shared memory data available\n");
        return -1;
    }

    // Check if a block is ready in the SHM ring buffer
    guint8 block_status = get_8(self->shm_data, self->consume_offset);
    
    if (block_status != 0) {
        rd = get_32(self->shm_data, self->consume_offset + 1);

        if (rd > 0) {
            data_processed = TRUE;
            
            // Copy data from SHM into the VCD loader's internal buffer with bounds checking
            size_t copy_size = rd;
            if (copy_size > VCD_BSIZ - 1) {
                copy_size = VCD_BSIZ - 1;
                g_printerr("WARNING: Truncating VCD data from %zu to %zu bytes\n", rd, copy_size);
            }
            
            for (size_t i = 0; i < copy_size; i++) {
                guint8 byte = get_8(self->shm_data, self->consume_offset + 5 + i);
                loader->vcdbuf[i] = byte;
            }
            loader->vcdbuf[copy_size] = 0;

            // Mark block as consumed
            put_8(self->shm_data, self->consume_offset, 0);
            self->consume_offset += (5 + rd);
            
            // Handle ring buffer wrap-around
            if (self->consume_offset >= RING_BUFFER_SIZE) {
                self->consume_offset %= RING_BUFFER_SIZE;
            }
            
            // Ensure consume_offset never becomes negative
            if (self->consume_offset < 0) {
                self->consume_offset += RING_BUFFER_SIZE;
            }
            
            g_printerr("DEBUG: Block ready, rd: %zu\n", rd);
            g_printerr("DEBUG: Copied %zu bytes to buffer: %s\n", rd, loader->vcdbuf);
        }
    }

    loader->vend = (loader->vst = loader->vcdbuf) + rd;

    if (!rd) {
        return -1; // EOF for this kick
    }
    
    if (data_processed) {
        g_printerr("DEBUG: Returning first byte: %d ('%c')\n", (int)(*loader->vst), *loader->vst);
    }
    return (int)(*loader->vst);
}



gboolean gw_vcd_partial_loader_kick(GwVcdPartialLoader *self)
{
    g_return_val_if_fail(GW_IS_VCD_PARTIAL_LOADER(self), FALSE);
    if (!self->shm_data) return FALSE;

    GwVcdLoader *loader = GW_VCD_LOADER(self);
    loader->vst = loader->vend = loader->vcdbuf;
    
    // Store self as user data for the callback
    loader->getch_fetch_override_data = self;
    loader->getch_fetch_override = vcd_partial_getch_fetch;
    loader->vcdbyteno = 0; // Reset byte counter for each kick
    
    // Actively call the parent's parser. It will use our override.
    GError *error = NULL;
    
    // Store initial time to detect if new data is processed
    GwTime initial_time = loader->current_time;
    
    vcd_parse(loader, &error);

    // Check if new data was processed (time advanced)
    gboolean data_processed = (loader->current_time > initial_time);
    
    // Only print debug messages if new data was processed
    if (data_processed) {
        g_printerr("DEBUG: Before parse - start: %ld, end: %ld, current: %ld\n",
                   loader->start_time, loader->end_time, initial_time);
        g_printerr("DEBUG: After parse - start: %ld, end: %ld, current: %ld\n",
                   loader->start_time, loader->end_time, loader->current_time);

        // After parsing, update the loader's time range
        if (loader->current_time > loader->end_time) {
            loader->end_time = loader->current_time;
            g_printerr("DEBUG: Updated end_time to: %ld\n", loader->end_time);
        }
    }

    loader->getch_fetch_override = NULL; // Unhook until next kick
    loader->getch_fetch_override_data = NULL;
    return data_processed;
}

void gw_vcd_partial_loader_cleanup(GwVcdPartialLoader *self)
{
    g_return_if_fail(GW_IS_VCD_PARTIAL_LOADER(self));
    if (self->shm) {
        gw_shared_memory_free(self->shm);
        self->shm = NULL;
        self->shm_data = NULL;
    }
    self->consume_offset = 0;
    self->header_parsed = FALSE;
    GW_VCD_LOADER(self)->current_time = 0;
    
    // Clean up the parent's buffer
    getch_free(GW_VCD_LOADER(self));
}

GwDumpFile *gw_vcd_partial_loader_load(GwVcdPartialLoader *self, const gchar *shm_id, GError **error)
{
    g_return_val_if_fail(GW_IS_VCD_PARTIAL_LOADER(self), NULL);

    self->shm = gw_shared_memory_open(shm_id, error);
    if (!self->shm) {
        return NULL;
    }
    self->shm_data = gw_shared_memory_get_data(self->shm);

    GwVcdLoader *loader = GW_VCD_LOADER(self);
    
    // Manually call the buffer allocation on our loader
    getch_alloc(loader);
    loader->time_vlist = gw_vlist_create(sizeof(GwTime));

    // Store self as user data for the callback
    loader->getch_fetch_override_data = self;
    loader->getch_fetch_override = vcd_partial_getch_fetch;

    // Parse the VCD header (this will stop after $enddefinitions in partial mode)
    vcd_parse(loader, error);


    // Clean up the override
    loader->getch_fetch_override = NULL;
    loader->getch_fetch_override_data = NULL;

    if (*error != NULL) {
        g_printerr("VCD parse error: %s\n", (*error)->message);
        getch_free(loader);
        return NULL;
    }

    // Check if the header was successfully parsed
    if (!loader->header_over) {
        g_set_error(error, GW_DUMP_FILE_ERROR, GW_DUMP_FILE_ERROR_NO_SYMBOLS, "VCD header not found in initial data stream.");
        getch_free(loader);
        return NULL;
    }

    // Reset numfacs to 0 before building symbols - vcd_build_symbols will
    // increment it as it processes each symbol component
    loader->numfacs = 0;



    if (loader->numsyms == 0) {

        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "VCD header parsing failed: no symbols found.");
        getch_free(loader);
        return NULL;
    }
    
    // Build symbols and facs like the regular VCD loader does
    vcd_build_symbols(loader);
    GwFacs *facs = vcd_sortfacs(loader);
    GwTree *tree = vcd_build_tree(loader, facs);
    // Create dump file with initial time range that can be updated later
    // Start with 0-0 range for header-only parsing
    GwTimeRange *initial_time_range = gw_time_range_new(0, 0);
    GwDumpFile *dump_file = g_object_new(GW_TYPE_VCD_FILE,
        "tree", tree,
        "facs", facs,
        "blackout-regions", loader->blackout_regions,
        "time-scale", loader->time_scale,
        "time-dimension", loader->time_dimension,
        "time-range", initial_time_range,
        NULL
    );
    g_object_unref(initial_time_range);

    // Clean up intermediate objects (they are now owned by the dump_file)
    g_object_unref(facs);
    g_object_unref(tree);

    // Mark header as parsed for future kicks
    self->header_parsed = TRUE;

    // Don't free the buffer yet - we'll need it for subsequent kicks
    return dump_file;
}

// Check if header has been parsed
gboolean gw_vcd_partial_loader_is_header_parsed(GwVcdPartialLoader *self)
{
    g_return_val_if_fail(GW_IS_VCD_PARTIAL_LOADER(self), FALSE);
    return self->header_parsed;
}

// Helper function to update time range after processing time data
void gw_vcd_partial_loader_update_time_range(GwVcdPartialLoader *self, GwDumpFile *dump_file)
{
    g_return_if_fail(GW_IS_VCD_PARTIAL_LOADER(self));
    g_return_if_fail(GW_IS_DUMP_FILE(dump_file));
    
    GwVcdLoader *loader = GW_VCD_LOADER(self);
    
    // Only update if we have valid time data
    if (loader->start_time >= 0 && loader->end_time >= loader->start_time) {
        // Update internal time range directly (time-range property is construct-only)
        GwVcdFile *vcd_file = GW_VCD_FILE(dump_file);

        vcd_file->start_time = loader->start_time;
        vcd_file->end_time = loader->end_time;

        // Also update the time range object by accessing its internal structure
        // This is a workaround for the CONSTRUCT_ONLY limitation
        GwTimeRange *time_range = gw_dump_file_get_time_range(dump_file);
        if (time_range != NULL) {
            // Access the internal structure directly (this is a hack but works for testing)
            GwTimeRangeInternal *tr = (GwTimeRangeInternal *)time_range;
            tr->start = loader->start_time;
            tr->end = loader->end_time;

        }
    }
}


