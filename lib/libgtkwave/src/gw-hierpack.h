#pragma once

#include <glib-object.h>
#include "gw-types.h"

G_BEGIN_DECLS

/**
 * GwHierpackContext:
 *
 * Context for hierarchy compression operations.
 *
 * This structure manages the state for compressing and decompressing
 * hierarchical signal names in waveform data.
 */
typedef struct _GwHierpackContext GwHierpackContext;

/**
 * gw_hierpack_context_new:
 *
 * Creates a new hierarchy compression context.
 *
 * Returns: (transfer full): A newly allocated #GwHierpackContext.
 * Free with gw_hierpack_context_free().
 */
GwHierpackContext *gw_hierpack_context_new(void);

/**
 * gw_hierpack_context_free:
 * @context: (transfer full): A #GwHierpackContext
 *
 * Frees a hierarchy compression context and all associated resources.
 */
void gw_hierpack_context_free(GwHierpackContext *context);

/**
 * gw_hierpack_compress:
 * @context: A #GwHierpackContext
 * @name: The hierarchical name to compress
 *
 * Compresses a hierarchical signal name using the context's compression
 * algorithm.
 *
 * Returns: (transfer full): The compressed name, or %NULL if compression
 * failed. Free with g_free().
 */
char *gw_hierpack_compress(GwHierpackContext *context, const char *name);

/**
 * gw_hierpack_decompress:
 * @context: A #GwHierpackContext
 * @compressed_name: The compressed name to decompress
 * @was_packed: (out) (optional): Set to %TRUE if the name was actually packed
 *
 * Decompresses a previously compressed hierarchical signal name.
 *
 * Returns: (transfer full): The decompressed name, or %NULL if decompression
 * failed. Free with g_free().
 */
char *gw_hierpack_decompress(GwHierpackContext *context,
                            const char *compressed_name,
                            gboolean *was_packed);

/**
 * gw_hierpack_get_compression_stats:
 * @context: A #GwHierpackContext
 * @compression_count: (out) (optional): Number of compression operations
 * @decompression_count: (out) (optional): Number of decompression operations
 * @total_original_bytes: (out) (optional): Total bytes of original data
 * @total_compressed_bytes: (out) (optional): Total bytes of compressed data
 * @elapsed_time: (out) (optional): Total elapsed time in seconds
 *
 * Retrieves performance statistics from the hierarchy compression context.
 */
void gw_hierpack_get_compression_stats(GwHierpackContext *context,
                                      guint *compression_count,
                                      guint *decompression_count,
                                      gsize *total_original_bytes,
                                      gsize *total_compressed_bytes,
                                      gdouble *elapsed_time);

/**
 * gw_hierpack_get_compression_ratio:
 * @context: A #GwHierpackContext
 *
 * Calculates the compression ratio (original/compressed).
 *
 * Returns: Compression ratio, or 1.0 if no compression occurred.
 */
gdouble gw_hierpack_get_compression_ratio(GwHierpackContext *context);

/**
 * gw_hierpack_reset_stats:
 * @context: A #GwHierpackContext
 *
 * Resets performance statistics while maintaining compression state.
 */
void gw_hierpack_reset_stats(GwHierpackContext *context);

G_END_DECLS