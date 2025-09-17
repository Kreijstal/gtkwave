#include "vcd_partial_adapter.h"
#include "globals.h"
#include "gw-vcd-partial-loader.h"
#include "gw-vcd-loader.h"
#include "gw-time-range.h"
#include "wavewindow.h" // For fix_wavehadj()
#include "menu.h"       // For redraw_signals_and_waves()
#include <stdio.h>

// A static pointer to hold our single loader instance for the current tab.
static GwVcdPartialLoader *the_loader = NULL;
static guint the_timer_id = 0;
static GwTime last_processed_time = 0;

// The timer callback that drives live updates.
static gboolean kick_timeout_callback(gpointer user_data)
{
    // If the loader was cleaned up, stop the timer.
    if (!the_loader) {
        the_timer_id = 0;
        return G_SOURCE_REMOVE; // Returning FALSE stops the timer
    }

    // Store initial time to detect if new data is processed
    GwTime initial_time = GLOBALS->tims.last;

    // "Kick" the loader to process any new data in the shared memory buffer.
    gw_vcd_partial_loader_kick(the_loader);

    // Update time range which will set the new end time
    gw_vcd_partial_loader_update_time_range(the_loader, GLOBALS->dump_file);

    if (GLOBALS->dump_file) {
        GwTimeRange *time_range = gw_dump_file_get_time_range(GLOBALS->dump_file);
        if (time_range) {
            GLOBALS->tims.last = gw_time_range_get_end(time_range);
        }
    }

    // Check if new data was processed (time advanced)
    gboolean data_processed = (GLOBALS->tims.last > initial_time);
    
    // Only print debug messages if new data was processed
    if (data_processed) {
        // Debug: Check dump file time range
        if (GLOBALS->dump_file) {
            GwTimeRange *time_range = gw_dump_file_get_time_range(GLOBALS->dump_file);
            if (time_range) {
                GwTime start = gw_time_range_get_start(time_range);
                GwTime end = gw_time_range_get_end(time_range);
                fprintf(stderr, "DEBUG: Dump file time range - start: %ld, end: %ld\n", start, end);
            }
        }

        // Debug: Check what the time range update did to global tims
        fprintf(stderr, "DEBUG: Global tims - last: %ld, first: %ld\n",
                GLOBALS->tims.last, GLOBALS->tims.first);

        fprintf(stderr, "DEBUG: Time check - initial: %ld, current: %ld, processed: %d\n",
                initial_time, GLOBALS->tims.last, data_processed);
        
        last_processed_time = GLOBALS->tims.last;
        fprintf(stderr, "DEBUG: Kicking partial loader (new data processed)\n");
        
        // Update the dump file's time range with any newly discovered times.
        gw_vcd_partial_loader_update_time_range(the_loader, GLOBALS->dump_file);

        // Simple harray rebuilding for interactive mode
        if (GLOBALS && GLOBALS->traces.first) {
            GwTrace *t = GLOBALS->traces.first;
            while (t) {
                if (!t->vector && t->n.nd && !t->n.nd->harray) {
                    // Build simple harray for this node
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
            //fprintf(stderr, "DEBUG: Updating UI\n"); stop fucking uncommenting it
            fix_wavehadj(); // Recalculate horizontal scrollbar range
            update_time_box();
            redraw_signals_and_waves();
        }
    }

    if (data_processed) {
        // Update the dump file's time range with any newly discovered times.
        gw_vcd_partial_loader_update_time_range(the_loader, GLOBALS->dump_file);

        // Simple harray rebuilding for interactive mode
        if (GLOBALS && GLOBALS->traces.first) {
            GwTrace *t = GLOBALS->traces.first;
            while (t) {
                if (!t->vector && t->n.nd && !t->n.nd->harray) {
                    // Build simple harray for this node
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
            //fprintf(stderr, "DEBUG: Updating UI\n");
            fix_wavehadj(); // Recalculate horizontal scrollbar range
            update_time_box();
            redraw_signals_and_waves();
        }
    }

    return G_SOURCE_CONTINUE; // Returning TRUE keeps the timer running
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