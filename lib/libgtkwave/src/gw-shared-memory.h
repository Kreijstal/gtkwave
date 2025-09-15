#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GwSharedMemory:
 *
 * Platform-agnostic shared memory abstraction.
 *
 * This structure provides a cross-platform interface for creating and
 * accessing shared memory segments, used for inter-process communication
 * in interactive waveform loading scenarios.
 */
typedef struct _GwSharedMemory GwSharedMemory;

/**
 * gw_shared_memory_create:
 * @id: Unique identifier for the shared memory segment
 * @size: Size of the shared memory segment in bytes
 *
 * Creates a new shared memory segment with the specified identifier and size.
 *
 * Returns: (transfer full): A newly allocated #GwSharedMemory, or %NULL on error.
 * Free with gw_shared_memory_free().
 */
GwSharedMemory *gw_shared_memory_create(const gchar *id, gsize size);

/**
 * gw_shared_memory_open:
 * @id: Unique identifier of the shared memory segment to open
 *
 * Opens an existing shared memory segment with the specified identifier.
 *
 * Returns: (transfer full): A #GwSharedMemory representing the opened segment,
 * or %NULL if the segment doesn't exist. Free with gw_shared_memory_free().
 */
GwSharedMemory *gw_shared_memory_open(const gchar *id);

/**
 * gw_shared_memory_free:
 * @shm: (transfer full): A #GwSharedMemory
 *
 * Frees a shared memory segment and releases all associated resources.
 * If this instance is the owner of the segment, the segment will be
 * destroyed. Otherwise, it will only be closed.
 */
void gw_shared_memory_free(GwSharedMemory *shm);

/**
 * gw_shared_memory_get_data:
 * @shm: A #GwSharedMemory
 *
 * Gets a pointer to the shared memory data.
 *
 * Returns: (transfer none): A pointer to the shared memory data, or %NULL
 * if the segment is not properly mapped.
 */
gpointer gw_shared_memory_get_data(GwSharedMemory *shm);

/**
 * gw_shared_memory_get_size:
 * @shm: A #GwSharedMemory
 *
 * Gets the size of the shared memory segment.
 *
 * Returns: The size of the shared memory segment in bytes.
 */
gsize gw_shared_memory_get_size(GwSharedMemory *shm);

/**
 * gw_shared_memory_is_owner:
 * @shm: A #GwSharedMemory
 *
 * Checks if this instance is the owner (creator) of the shared memory segment.
 *
 * Returns: %TRUE if this instance created the segment, %FALSE otherwise.
 */
gboolean gw_shared_memory_is_owner(GwSharedMemory *shm);

G_END_DECLS