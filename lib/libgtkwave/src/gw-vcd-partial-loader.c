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

#include "gw-vcd-partial-loader.h"
#include "gw-vcd-loader-private.h" // We need access to the parent's guts
#include "gw-vcd-file.h"
#include "gw-vcd-file-private.h" // For accessing internal GwVcdFile members
#include "gw-vcd-loader.h" // For accessing the loader structure

#include "gw-facs.h"
#include "gw-tree.h"
#include "gw-time-range.h"
#include <errno.h>
#include <gio/gio.h>

// Forward declaration of GwTimeRange internal structure for direct access
typedef struct _GwTimeRangeInternal {
    GObject parent_instance;
    GwTime start;
    GwTime end;
} GwTimeRangeInternal;

#define RING_BUFFER_SIZE (1024 * 1024)

struct _GwVcdPartialLoader
{
    GwVcdLoader parent_instance;

    // SHM state
    GwSharedMemory *shm;
    guint8 *shm_data;
    gssize consume_offset;
    
    // Track whether we've already parsed the header
    gboolean header_parsed;
};

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

    if (!self->shm_data) {
        g_printerr("No shared memory data available\n");
        return -1;
    }

    // Scan for the next available block in the ring buffer
    gssize scan_offset = self->consume_offset;
    guint8 block_status = get_8(self->shm_data, scan_offset);
    
    g_printerr("DEBUG: getch_fetch called, scan_offset: %ld, block_status: %u, shm_data: %p\n",
               scan_offset, block_status, self->shm_data);

    // If current block is consumed (status 0), scan for next available block
    if (block_status == 0) {
        g_printerr("DEBUG: Scanning for next available block...\n");

        // Scan for the next available block, but limit the scan to avoid infinite loops
        gssize current_offset = scan_offset;
        gssize bytes_scanned = 0;
        gboolean found_block = FALSE;

        // Scan at most the entire buffer size to prevent infinite loops
        while (bytes_scanned < RING_BUFFER_SIZE) {
            // Read the length of the current block
            guint32 block_length = get_32(self->shm_data, current_offset + 1);

            // If block_length is 0, this might be an invalid block or end of data
            // Use a minimum skip to avoid infinite loops
            if (block_length == 0) {
                // Skip status + 4-byte length
                current_offset += 5;
                bytes_scanned += 5;
            } else {
                // Move to the next block
                current_offset += 1 + 4 + block_length;
                bytes_scanned += 1 + 4 + block_length;
            }

            // Handle ring buffer wrap-around
            if (current_offset >= RING_BUFFER_SIZE) {
                current_offset %= RING_BUFFER_SIZE;
            }

            // Check the status of the next block
            block_status = get_8(self->shm_data, current_offset);

            if (block_status != 0) {
                found_block = TRUE;
                break;
            }

            // If we've wrapped around to the starting point, stop scanning
            if (current_offset == scan_offset) {
                g_printerr("DEBUG: Wrapped around to starting offset, no available blocks\n");
                break;
            }
        }

        if (found_block) {
            // Found an available block, update consume_offset
            self->consume_offset = current_offset;
            g_printerr("DEBUG: Found available block at offset %ld\n", self->consume_offset);
        } else {
            g_printerr("DEBUG: No available blocks found after scanning %ld bytes\n", bytes_scanned);
            loader->vend = (loader->vst = loader->vcdbuf) + 0;
            return -1;
        }
    }
    
    if (block_status != 0) {
        rd = get_32(self->shm_data, self->consume_offset + 1);
        g_printerr("DEBUG: Block ready, rd: %zu\n", rd);

        if (rd > 0) {
            // Copy data from SHM into the VCD loader's internal buffer
            for (size_t i = 0; i < rd; i++) {
                guint8 byte = get_8(self->shm_data, self->consume_offset + 5 + i);
                loader->vcdbuf[i] = byte;
            }
            loader->vcdbuf[rd] = 0;

            // Mark block as consumed
            self->shm_data[self->consume_offset % RING_BUFFER_SIZE] = 0;
            self->consume_offset += (5 + rd);
            
            // Handle ring buffer wrap-around
            if (self->consume_offset >= RING_BUFFER_SIZE) {
                self->consume_offset %= RING_BUFFER_SIZE;
            }
            
            g_printerr("DEBUG: Copied %zu bytes to buffer: %s\n", rd, loader->vcdbuf);
        }
    }

    loader->vend = (loader->vst = loader->vcdbuf) + rd;

    if (!rd) {
        g_printerr("DEBUG: No data available, returning -1\n");
        return -1; // EOF for this kick
    }
    g_printerr("DEBUG: Returning first byte: %d ('%c')\n", (int)(*loader->vst), *loader->vst);
    return (int)(*loader->vst);
}

void gw_vcd_partial_loader_kick(GwVcdPartialLoader *self)
{
    g_return_if_fail(GW_IS_VCD_PARTIAL_LOADER(self));
    if (!self->shm_data) return;

    GwVcdLoader *loader = GW_VCD_LOADER(self);
    
    // Store self as user data for the callback
    loader->getch_fetch_override_data = self;
    loader->getch_fetch_override = vcd_partial_getch_fetch;
    loader->vcdbyteno = 0; // Reset byte counter for each kick
    
    // Actively call the parent's parser. It will use our override.
    GError *error = NULL;
    
    // Debug: Print times before parsing
    g_printerr("DEBUG: Before parse - start: %ld, end: %ld, current: %ld\n",
               loader->start_time, loader->end_time, loader->current_time);
    
    vcd_parse(loader, &error);
    
    // Debug: Print times after parsing
    g_printerr("DEBUG: After parse - start: %ld, end: %ld, current: %ld\n",
               loader->start_time, loader->end_time, loader->current_time);

    // After parsing, update the loader's time range
    if (loader->current_time > loader->end_time) {
        loader->end_time = loader->current_time;
        g_printerr("DEBUG: Updated end_time to: %ld\n", loader->end_time);
    }

    loader->getch_fetch_override = NULL; // Unhook until next kick
    loader->getch_fetch_override_data = NULL;
    

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
    
    // Debug: Print loader time values and consume offset
    g_printerr("DEBUG: Loader times - start: %ld, end: %ld, current: %ld, consume_offset: %ld\n",
               loader->start_time, loader->end_time, loader->current_time, self->consume_offset);
    
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
        g_printerr("DEBUG: Time range updated to start: %ld, end: %ld\n",
                   loader->start_time, loader->end_time);
    } else {
        g_printerr("DEBUG: Time range not updated - invalid time data\n");
    }
}