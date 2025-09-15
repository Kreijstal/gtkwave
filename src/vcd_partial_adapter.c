#include "vcd_partial_adapter.h"
#include "globals.h"
#include "gw-vcd-partial-loader.h"
#include "gw-facs.h"
#include "gw-symbol.h"
#include "gw-vcd-loader-private.h"
#include <glib-object.h>
#include "symbol.h"
#include "tree.h"
#include "gw-facs.h"
#include "debug.h"
#include "analyzer.h"

/* Global pointer to the partial loader instance */
static GwVcdPartialLoader *partial_loader = NULL;

GwTime vcd_partial_main(char *fname)
{

    
    /* Create new partial loader instance */
    GwLoader *loader = gw_vcd_partial_loader_new();

    partial_loader = GW_VCD_PARTIAL_LOADER(loader);
    g_object_ref(loader); /* Increase ref count to keep loader alive */
    
    /* Set common settings like other loader functions */
    const Settings *global_settings = &GLOBALS->settings;
    gw_loader_set_preserve_glitches(loader, global_settings->preserve_glitches);
    gw_loader_set_preserve_glitches_real(loader, global_settings->preserve_glitches_real);

    if (!GLOBALS->hier_was_explicitly_set) {
        GLOBALS->hier_delimeter = '.';
    }
    gw_loader_set_hierarchy_delimiter(loader, GLOBALS->hier_delimeter);
    
    /* Configure shared memory from filename */
    guint shmidu = ~0U;
    if (!strcmp(fname, "-vcd")) {
        /* Special case for stdin VCD - use default shared memory ID */
        shmidu = ~0U;
    } else {
        /* Parse shared memory ID from filename */
        int parsed = sscanf(fname, "%x", &shmidu);
        if (parsed != 1) {
            shmidu = ~0U;
        }
    }


    
    gw_vcd_partial_loader_set_shared_memory_id(partial_loader, shmidu);

    gw_vcd_partial_loader_set_streaming_enabled(partial_loader, TRUE);

    
    /* Set global flags for compatibility */
    GLOBALS->partial_vcd = ~0;
    GLOBALS->is_vcd = ~0;

    
    /* Load the partial VCD - use special indicator for shared memory */

    GError *error = NULL;
    GwDumpFile *file = gw_loader_load(loader, "-vcd", &error);

    if (file) {

    }
    fflush(stderr);
    
    if (error) {

        g_error_free(error);
        g_object_unref(loader);
        partial_loader = NULL;
        return GW_TIME_CONSTANT(0);
    } else {

    }
    

    if (file) {



        
        /* Check if we have symbols */
        GwFacs *facs = gw_dump_file_get_facs(file);
        if (facs) {
            guint symbol_count = gw_facs_get_length(facs);

        }
        
        /* Store the dump file in global state for SST population */
        if (GLOBALS->dump_file) {
            g_object_unref(GLOBALS->dump_file);
        }
        GLOBALS->dump_file = file;
        /* Don't call g_object_ref here - we're taking ownership of the reference returned by gw_loader_load */


    }
    
    /* Don't unref the loader here - we're keeping it alive for kick_partial_vcd */
    /* The reference will be cleaned up in vcd_partial_cleanup */
    
    /* Return max time for compatibility */

    GwVcdLoader *vcd_loader = GW_VCD_LOADER(partial_loader);

    if (vcd_loader) {

    } else {

    }
    
    return vcd_loader ? vcd_loader->end_time : GW_TIME_CONSTANT(0);
}

void kick_partial_vcd(void)
{

    if (partial_loader && GLOBALS->partial_vcd) {
        gw_vcd_partial_loader_kick(partial_loader);
    }
}

void vcd_partial_mark_and_sweep(int mandclear)
{

    if (partial_loader) {
        gw_vcd_partial_loader_mark_and_sweep(partial_loader, (gboolean)mandclear);
    }
}

/* Cleanup function */
void vcd_partial_cleanup(void)
{

    if (partial_loader) {
        GwLoader *loader = GW_LOADER(partial_loader);
        g_object_unref(loader);
        partial_loader = NULL;
    }
}