#pragma once

#include "gw-types.h"
#include "gw-hist-ent.h"
#include <glib.h>

G_BEGIN_DECLS

/**
 * GwNodeHistory:
 *
 * An opaque structure containing the history data for a GwNode.
 * This structure is designed to be used with atomic reference counting
 * to enable thread-safe Read-Copy-Update (RCU) semantics.
 */
typedef struct _GwNodeHistory GwNodeHistory;

/**
 * gw_node_history_new:
 *
 * Creates a new GwNodeHistory with reference count of 1.
 *
 * Returns: (transfer full): A new GwNodeHistory
 */
GwNodeHistory *gw_node_history_new(void);

/**
 * gw_node_history_ref:
 * @self: A GwNodeHistory
 *
 * Atomically increments the reference count.
 *
 * Returns: (transfer none): The same GwNodeHistory for convenience
 */
GwNodeHistory *gw_node_history_ref(GwNodeHistory *self);

/**
 * gw_node_history_unref:
 * @self: (nullable): A GwNodeHistory
 *
 * Atomically decrements the reference count and frees the structure
 * if the count reaches zero.
 */
void gw_node_history_unref(GwNodeHistory *self);

/**
 * gw_node_history_get_head:
 * @self: A GwNodeHistory
 *
 * Gets a pointer to the head of the history linked list.
 * The caller must hold a reference to the history while using this pointer.
 *
 * Returns: (transfer none): Pointer to the head history entry
 */
GwHistEnt *gw_node_history_get_head(GwNodeHistory *self);

/**
 * gw_node_history_get_curr:
 * @self: A GwNodeHistory
 *
 * Gets a pointer to the current (last) history entry.
 * The caller must hold a reference to the history while using this pointer.
 *
 * Returns: (transfer none): Pointer to the current history entry
 */
GwHistEnt *gw_node_history_get_curr(GwNodeHistory *self);

/**
 * gw_node_history_get_harray:
 * @self: A GwNodeHistory
 *
 * Gets the harray (fast lookup array) for binary search.
 * The caller must hold a reference to the history while using this pointer.
 *
 * Returns: (transfer none): Pointer to the harray
 */
GwHistEnt **gw_node_history_get_harray(GwNodeHistory *self);

/**
 * gw_node_history_get_numhist:
 * @self: A GwNodeHistory
 *
 * Gets the number of history entries.
 *
 * Returns: Number of history entries
 */
int gw_node_history_get_numhist(GwNodeHistory *self);

/**
 * gw_node_history_copy_from:
 * @dest: Destination GwNodeHistory
 * @src: Source GwNodeHistory
 *
 * Deep copies the history data from src to dest.
 * This includes copying the linked list and harray.
 */
void gw_node_history_copy_from(GwNodeHistory *dest, GwNodeHistory *src);

/**
 * gw_node_history_set_head:
 * @self: A GwNodeHistory
 * @head: The new head entry
 *
 * Sets the head entry. Note: This does not copy the entry,
 * the history takes ownership of managing the linked list.
 */
void gw_node_history_set_head(GwNodeHistory *self, GwHistEnt head);

/**
 * gw_node_history_set_curr:
 * @self: A GwNodeHistory
 * @curr: Pointer to the current entry
 *
 * Sets the curr pointer.
 */
void gw_node_history_set_curr(GwNodeHistory *self, GwHistEnt *curr);

/**
 * gw_node_history_append_entry:
 * @self: A GwNodeHistory
 * @hent: The history entry to append
 *
 * Appends a new history entry to the linked list and updates curr.
 * Does NOT update the harray - call regenerate_harray after all appends.
 */
void gw_node_history_append_entry(GwNodeHistory *self, GwHistEnt *hent);

/**
 * gw_node_history_regenerate_harray:
 * @self: A GwNodeHistory
 *
 * Regenerates the harray from the current linked list.
 * This ensures consistency between numhist and harray.
 */
void gw_node_history_regenerate_harray(GwNodeHistory *self);

G_END_DECLS
