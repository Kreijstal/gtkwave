#include "vcd_partial_adapter.h"
#include "globals.h"
#include "analyzer.h" /* For AddNodeTraceReturn, HasWave */
#include "gw-vcd-partial-loader.h"
#include "gw-vcd-loader.h"
#include "gw-dump-file.h"
#include "gw-time-range.h"
#include "wavewindow.h" // For fix_wavehadj()
#include "menu.h"       // For redraw_signals_and_waves()
#include <stdio.h>

// A static pointer to hold our single loader instance for the current tab.
static GwVcdPartialLoader *the_loader = NULL;
static guint the_timer_id = 0;

// The timer callback that drives live updates.
static gboolean kick_timeout_callback(gpointer user_data)
{
    if (!the_loader || !GLOBALS->dump_file) {
        the_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    GwTime previous_end_time = gw_time_range_get_end(gw_dump_file_get_time_range(GLOBALS->dump_file));

    /* STEP 1: Kick the loader to update the underlying vlists with new data. */
    gw_vcd_partial_loader_kick(the_loader);

    /* Check if new data was actually processed by checking for time advancement */
    gw_vcd_partial_loader_update_time_range(the_loader, GLOBALS->dump_file);
    GwTime current_end_time = gw_time_range_get_end(gw_dump_file_get_time_range(GLOBALS->dump_file));

    if (current_end_time > previous_end_time) {
        /* STEP 2: Iterate through all currently displayed traces and force a re-import. */
        GwTrace *t = GLOBALS->traces.first;
        while (t) {
            if (!t->vector && HasWave(t)) {
                /*
                 * Free the old harray. This marks the trace for re-import.
                 * The AddNodeTraceReturn function (or a similar mechanism) will
                 * see that the harray is missing and rebuild it from the
                 * updated vlist data.
                 */
                if (t->n.nd->harray) {
                    free_2(t->n.nd->harray);
                    t->n.nd->harray = NULL;
                    t->n.nd->numhist = 0;
                }

                /*
                 * Re-import the trace. This will decompress the *entire updated* vlist
                 * for this node and rebuild the harray.
                 */
                gw_dump_file_import_traces(GLOBALS->dump_file, (GwNode **)&(t->n.nd), NULL);
            }
            /* TODO: Handle vectors as well. */
            t = t->t_next;
        }

        /* STEP 3: Update the UI with the newly rebuilt data. */
        fix_wavehadj();
        update_time_box();
        redraw_signals_and_waves();
    }

    return G_SOURCE_CONTINUE;
}

GwDumpFile *vcd_partial_main(const gchar *shm_id)
{
    fprintf(stderr, "DEBUG: Starting interactive VCD session with SHM ID: %s\n", shm_id);
    fprintf(stderr, "DEBUG: Before cleanup - the_loader: %p, the_timer_id: %u\n", the_loader, the_timer_id);
    vcd_partial_cleanup(); // Clean up any previous instance
    fprintf(stderr, "DEBUG: After cleanup - the_loader: %p, the_timer_id: %u\n", the_loader, the_timer_id);
    
    // Set the partial_vcd flag to indicate interactive mode
    GLOBALS->partial_vcd = 1;

    the_loader = gw_vcd_partial_loader_new();
    fprintf(stderr, "DEBUG: Created partial loader: %p\n", the_loader);
    GError *error = NULL;

    // The load function performs the initial header parse.
    fprintf(stderr, "DEBUG: Loading initial VCD data with loader: %p\n", the_loader);
    GwDumpFile *dump_file = gw_vcd_partial_loader_load(the_loader, shm_id, &error);
    fprintf(stderr, "DEBUG: Load result - dump_file: %p, error: %p\n", dump_file, error);

    if (error) {
        fprintf(stderr, "DEBUG: Load failed: %s\n", error->message);
        // Display an error dialog to the user
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(GLOBALS->mainwindow),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_CLOSE,
                                                   "Failed to start interactive VCD session: %s",
                                                   error->message);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_error_free(error);
        
        gw_vcd_partial_loader_cleanup(the_loader);
        g_object_unref(the_loader);
        the_loader = NULL;
        return NULL;
    }

    // Start the periodic timer. It will call kick_timeout_callback every 100ms.
    // Use a longer initial delay to ensure GUI is fully initialized.
    fprintf(stderr, "DEBUG: Starting timer for periodic updates\n");
    the_timer_id = g_timeout_add(500, kick_timeout_callback, NULL);

    fprintf(stderr, "DEBUG: Interactive VCD session started successfully, returning dump_file: %p\n", dump_file);
    return dump_file;
}

void vcd_partial_cleanup(void)
{
    fprintf(stderr, "DEBUG: Cleaning up partial loader - the_loader: %p, the_timer_id: %u\n", the_loader, the_timer_id);
    if (the_timer_id > 0) {
        fprintf(stderr, "DEBUG: Removing timer ID: %u\n", the_timer_id);
        g_source_remove(the_timer_id);
        the_timer_id = 0;
    }

    if (the_loader) {
        fprintf(stderr, "DEBUG: Cleaning up loader\n");
        gw_vcd_partial_loader_cleanup(the_loader);
        g_object_unref(the_loader);
        the_loader = NULL;
    }
    fprintf(stderr, "DEBUG: Partial loader cleanup complete\n");
}