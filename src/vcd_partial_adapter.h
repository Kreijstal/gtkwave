#ifndef VCD_PARTIAL_ADAPTER_H
#define VCD_PARTIAL_ADAPTER_H

#include "gw-dump-file.h"

G_BEGIN_DECLS

/**
 * vcd_partial_main:
 * @shm_id: The shared memory identifier string.
 *
 * The main entry point for initializing an interactive VCD session.
 * This function creates and configures the partial loader, performs the
 * initial load to get the symbol hierarchy, and starts a periodic timer
 * to check for new waveform data.
 *
 * Returns: (transfer full): The initially populated #GwDumpFile, or %NULL on failure.
 */
GwDumpFile *vcd_partial_main(const gchar *shm_id);

/**
 * vcd_partial_cleanup:
 *
 * Stops the interactive loading timer and cleans up all resources
 * associated with the partial loader. Should be called when the
 * interactive tab is closed or the application exits.
 */
void vcd_partial_cleanup(void);

G_END_DECLS

#endif /* VCD_PARTIAL_ADAPTER_H */