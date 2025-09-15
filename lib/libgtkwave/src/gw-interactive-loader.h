#pragma once

#include <glib-object.h>
#include "gw-loader.h"

G_BEGIN_DECLS

/**
 * GwInteractiveLoader:
 *
 * Interface for interactive waveform data loaders.
 *
 * This interface extends #GwLoader to support interactive loading
 * of waveform data, allowing for incremental processing and real-time
 * updates during data acquisition.
 */
#define GW_TYPE_INTERACTIVE_LOADER (gw_interactive_loader_get_type())
G_DECLARE_INTERFACE(GwInteractiveLoader, gw_interactive_loader, GW, INTERACTIVE_LOADER, GwLoader)

/**
 * GwInteractiveLoaderInterface:
 * @parent_interface: The parent interface
 * @process_data: Processes incoming waveform data chunks
 * @is_complete: Checks if loading is complete
 * @kick_processing: Forces processing of buffered data
 * @cleanup: Cleans up resources after loading
 *
 * Interface structure for #GwInteractiveLoader.
 */
struct _GwInteractiveLoaderInterface {
    GTypeInterface parent_interface;
    
    /**
     * process_data:
     * @loader: A #GwInteractiveLoader
     * @data: The data to process
     * @length: Length of the data
     *
     * Processes a chunk of waveform data. This method should be called
     * incrementally as data becomes available.
     *
     * Returns: %TRUE if processing succeeded, %FALSE otherwise
     */
    gboolean (*process_data)(GwInteractiveLoader *loader, const gchar *data, gsize length);
    
    /**
     * is_complete:
     * @loader: A #GwInteractiveLoader
     *
     * Checks if the interactive loading process is complete.
     *
     * Returns: %TRUE if loading is complete, %FALSE otherwise
     */
    gboolean (*is_complete)(GwInteractiveLoader *loader);
    
    /**
     * kick_processing:
     * @loader: A #GwInteractiveLoader
     *
     * Forces processing of any buffered data. This can be used to
     * ensure that all available data is processed immediately.
     */
    void (*kick_processing)(GwInteractiveLoader *loader);
    
    /**
     * cleanup:
     * @loader: A #GwInteractiveLoader
     *
     * Cleans up resources after loading is complete. This should be
     * called when interactive loading is finished to release any
     * temporary resources.
     */
    void (*cleanup)(GwInteractiveLoader *loader);
};

/**
 * gw_interactive_loader_process_data:
 * @loader: A #GwInteractiveLoader
 * @data: The data to process
 * @length: Length of the data
 *
 * Processes a chunk of waveform data incrementally.
 *
 * Returns: %TRUE if processing succeeded, %FALSE otherwise
 */
gboolean gw_interactive_loader_process_data(GwInteractiveLoader *loader, const gchar *data, gsize length);

/**
 * gw_interactive_loader_is_complete:
 * @loader: A #GwInteractiveLoader
 *
 * Checks if the interactive loading process is complete.
 *
 * Returns: %TRUE if loading is complete, %FALSE otherwise
 */
gboolean gw_interactive_loader_is_complete(GwInteractiveLoader *loader);

/**
 * gw_interactive_loader_kick_processing:
 * @loader: A #GwInteractiveLoader
 *
 * Forces processing of any buffered data.
 */
void gw_interactive_loader_kick_processing(GwInteractiveLoader *loader);

/**
 * gw_interactive_loader_cleanup:
 * @loader: A #GwInteractiveLoader
 *
 * Cleans up resources after interactive loading is complete.
 */
void gw_interactive_loader_cleanup(GwInteractiveLoader *loader);

G_END_DECLS