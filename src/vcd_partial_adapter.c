#include "vcd_partial_adapter.h"
#include "globals.h"
#include "gw-vcd-partial-loader.h"
#include "gw-vcd-loader.h"
#include "gw-time-range.h"
#include "wavewindow.h" // For fix_wavehadj()
#include "menu.h"       // For redraw_signals_and_waves()
#include <stdio.h>
#include <glib.h>
#include <inttypes.h>

// A static pointer to hold our single loader instance for the current tab.
static GwVcdPartialLoader *the_loader = NULL;
static guint the_timer_id = 0;
static GwTime last_processed_time = 0;
static GMutex loader_mutex;

// The timer callback that drives live updates.
static gboolean kick_timeout_callback(gpointer user_data)
{
    // Lock the mutex to prevent race conditions with cleanup
    g_mutex_lock(&loader_mutex);
    
    // If the loader was cleaned up, stop the timer.
    if (!the_loader) {
        g_mutex_unlock(&loader_mutex);
        the_timer_id = 0;
        return G_SOURCE_REMOVE; // Returning FALSE stops the timer
    }

    // Additional safety check - if globals are not initialized, bail out
    if (!GLOBALS) {
        g_mutex_unlock(&loader_mutex);
        return G_SOURCE_REMOVE;
    }

    // Store initial time to detect if new data is processed
    GwTime initial_time = GLOBALS->tims.last;

    // "Kick" the loader to process any new data in the shared memory buffer.
    // Check again if the_loader is still valid after the mutex was unlocked
    if (!the_loader || !GLOBALS) {
        g_mutex_unlock(&loader_mutex);
        return G_SOURCE_REMOVE;
    }
    gw_vcd_partial_loader_kick(the_loader);

    // Update time range which will set the new end time
    // Check again if the_loader is still valid
    if (!the_loader || !GLOBALS || !GLOBALS->dump_file) {
        g_mutex_unlock(&loader_mutex);
        return G_SOURCE_REMOVE;
    }
    gw_vcd_partial_loader_update_time_range(the_loader, GLOBALS->dump_file);

    if (GLOBALS && GLOBALS->dump_file) {
        GwTimeRange *time_range = gw_dump_file_get_time_range(GLOBALS->dump_file);
        if (time_range) {
            GLOBALS->tims.last = gw_time_range_get_end(time_range);
        }
    }


    // Check if new data was processed (time advanced)
    gboolean data_processed = (GLOBALS && (GLOBALS->tims.last > initial_time));
    
    if (data_processed) {
        // Check again if the_loader is still valid before accessing it
        if (!the_loader || !GLOBALS) {
            g_mutex_unlock(&loader_mutex);
            return G_SOURCE_REMOVE;
        }
        
        /* Print a user-friendly message indicating that new data has been processed. */
        fprintf(stdout, "INFO: New VCD data processed. New end time: %"PRId64"\n", GLOBALS->tims.last);
        
        // Check again if the_loader is still valid before accessing it
        if (!the_loader || !GLOBALS || !GLOBALS->dump_file) {
            g_mutex_unlock(&loader_mutex);
            return G_SOURCE_REMOVE;
        }
        
        // Update the dump file's time range with any newly discovered times.
        gw_vcd_partial_loader_update_time_range(the_loader, GLOBALS->dump_file);

        // Import traces to convert vlists to proper history entries
        if (GLOBALS && GLOBALS->dump_file && GLOBALS->traces.first && GLOBALS->traces.total > 0) {
            // Build a list of nodes that need importing
            GwNode **nodes = malloc_2((GLOBALS->traces.total + 1) * sizeof(GwNode *));
            int i = 0;
            GwTrace *t = GLOBALS->traces.first;
            
            while (t && i < GLOBALS->traces.total) {
                if (t->n.nd && t->n.nd->mv.mvlfac_vlist != NULL) {
                    nodes[i++] = t->n.nd;
                }
                t = t->t_next;
            }
            nodes[i] = NULL; // NULL-terminate the array
            
            if (i > 0) {
                GError *error = NULL;
                if (!gw_dump_file_import_traces(GLOBALS->dump_file, nodes, &error)) {
                    fprintf(stderr, "Failed to import traces: %s\n", error ? error->message : "Unknown error");
                    if (error) g_error_free(error);
                }
            }
            
            free_2(nodes);
        }

        // Rebuild harray for ALL visible traces to ensure UI consistency
        if (GLOBALS && GLOBALS->traces.first) {
            GwTrace *t = GLOBALS->traces.first;
            while (t) {
                if (!t->vector && HasWave(t) && t->n.nd && t->n.nd->harray) {
                    // Free the old harray if it exists
                    if (t->n.nd->harray) {
                        free_2(t->n.nd->harray);
                        t->n.nd->harray = NULL;
                    }
                    
                    // Build new harray from the current linked list
                    GwHistEnt *histpnt = &(t->n.nd->head);
                    int histcount = 0;
                    
                    while (histpnt) {
                        histcount++;
                        histpnt = histpnt->next;
                    }
                    
                    t->n.nd->numhist = histcount;
                    if (histcount > 0) {
                        t->n.nd->harray = malloc_2(histcount * sizeof(GwHistEnt *));
                        histpnt = &(t->n.nd->head);
                        histcount = 0;
                        
                        while (histpnt) {
                            t->n.nd->harray[histcount++] = histpnt;
                            histpnt = histpnt->next;
                        }
                    }
                }
                t = t->t_next;
            }
        }

        // Update the UI to reflect the new data, but only if GUI is initialized
        if (GLOBALS && GLOBALS->mainwindow) {
            //fprintf(stderr, "DEBUG: Updating UI\n"); // stop fucking uncommenting it
            fix_wavehadj(); // Recalculate horizontal scrollbar range
            update_time_box();
            redraw_signals_and_waves();
        }
    }

    g_mutex_unlock(&loader_mutex);
    return G_SOURCE_CONTINUE; // Returning TRUE keeps the timer running
}

GwDumpFile *vcd_partial_main(const gchar *shm_id)
{
    vcd_partial_cleanup(); // Clean up any previous instance
    
    // Lock the mutex during initialization
    g_mutex_lock(&loader_mutex);
    
    // Set the partial_vcd flag to indicate interactive mode
    GLOBALS->partial_vcd = 1;

    the_loader = gw_vcd_partial_loader_new();
    GError *error = NULL;

    // The load function performs the initial header parse.
    GwDumpFile *dump_file = gw_vcd_partial_loader_load(the_loader, shm_id, &error);

    if (error) {
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
    g_mutex_unlock(&loader_mutex);
    return dump_file;
}

void vcd_partial_cleanup(void)
{
    // Lock the mutex during cleanup to prevent race conditions
    g_mutex_lock(&loader_mutex);
    if (the_timer_id > 0) {
        g_source_remove(the_timer_id);
        the_timer_id = 0;
    }

    if (the_loader) {
        gw_vcd_partial_loader_cleanup(the_loader);
        g_object_unref(the_loader);
        the_loader = NULL;
    }
    g_mutex_unlock(&loader_mutex);
}