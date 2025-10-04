#include "gw-node-history.h"
#include <stdlib.h>
#include <string.h>

struct _GwNodeHistory
{
    gint ref_count; // Atomic reference count for RCU

    // The actual history data
    GwHistEnt head;
    GwHistEnt *curr;
    GwHistEnt **harray;
    int numhist;
};

GwNodeHistory *gw_node_history_new(void)
{
    GwNodeHistory *self = g_new0(GwNodeHistory, 1);
    self->ref_count = 1; // Start with reference count of 1
    self->head.time = -1;
    self->head.next = NULL;
    self->curr = &self->head;
    self->harray = NULL;
    self->numhist = 0;
    return self;
}

GwNodeHistory *gw_node_history_ref(GwNodeHistory *self)
{
    g_return_val_if_fail(self != NULL, NULL);
    g_atomic_int_inc(&self->ref_count);
    return self;
}

void gw_node_history_unref(GwNodeHistory *self)
{
    if (self == NULL) {
        return;
    }

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        // Free all history entries in the linked list (except head which is inline)
        GwHistEnt *curr = self->head.next;
        while (curr != NULL) {
            GwHistEnt *next = curr->next;
            
            // Free vector/string data if present
            // Only free h_vector for strings. For non-string vectors, we would need
            // to know if it's really a vector vs a scalar value. Since this is a 
            // union, we can't reliably distinguish without additional metadata.
            // The caller must manage vector memory appropriately.
            if (curr->flags & GW_HIST_ENT_FLAG_STRING) {
                g_free(curr->v.h_vector);
            }
            
            g_free(curr);
            curr = next;
        }

        // Free harray
        g_free(self->harray);

        // Free the structure itself
        g_free(self);
    }
}

GwHistEnt *gw_node_history_get_head(GwNodeHistory *self)
{
    g_return_val_if_fail(self != NULL, NULL);
    return &self->head;
}

GwHistEnt *gw_node_history_get_curr(GwNodeHistory *self)
{
    g_return_val_if_fail(self != NULL, NULL);
    return self->curr;
}

GwHistEnt **gw_node_history_get_harray(GwNodeHistory *self)
{
    g_return_val_if_fail(self != NULL, NULL);
    return self->harray;
}

int gw_node_history_get_numhist(GwNodeHistory *self)
{
    g_return_val_if_fail(self != NULL, 0);
    return self->numhist;
}

void gw_node_history_set_head(GwNodeHistory *self, GwHistEnt head)
{
    g_return_if_fail(self != NULL);
    self->head = head;
}

void gw_node_history_set_curr(GwNodeHistory *self, GwHistEnt *curr)
{
    g_return_if_fail(self != NULL);
    self->curr = curr;
}

void gw_node_history_copy_from(GwNodeHistory *dest, GwNodeHistory *src)
{
    g_return_if_fail(dest != NULL);
    g_return_if_fail(src != NULL);

    // Copy the head entry (this is an inline struct, not a pointer)
    dest->head = src->head;
    dest->head.next = NULL; // Will rebuild the chain with shared entries
    
    if (src->head.next == NULL) {
        dest->curr = &dest->head;
        dest->numhist = 0;
        g_free(dest->harray);
        dest->harray = NULL;
        return;
    }

    // Share the history entries (they are immutable after creation).
    // We copy the linked list structure but share the GwHistEnt objects.
    // This is safe for RCU because old snapshots keep their references.
    GwHistEnt *src_ent = src->head.next;
    GwHistEnt *dest_prev = &dest->head;
    int count = 1; // Count the head

    while (src_ent != NULL) {
        // Share the entry - don't allocate new
        dest_prev->next = src_ent;
        dest_prev = src_ent;
        count++;
        src_ent = src_ent->next;
    }

    dest->curr = dest_prev;
    dest->numhist = count;

    // Copy harray if it exists
    if (src->harray != NULL && src->numhist > 0) {
        dest->harray = g_new(GwHistEnt *, src->numhist);
        // Can reuse the same harray entries since we're sharing the GwHistEnt objects
        memcpy(dest->harray, src->harray, src->numhist * sizeof(GwHistEnt *));
    } else {
        dest->harray = NULL;
    }
}

void gw_node_history_append_entry(GwNodeHistory *self, GwHistEnt *hent)
{
    g_return_if_fail(self != NULL);
    g_return_if_fail(hent != NULL);

    // Append to the linked list
    self->curr->next = hent;
    self->curr = hent;
    self->numhist++;
}

void gw_node_history_regenerate_harray(GwNodeHistory *self)
{
    g_return_if_fail(self != NULL);

    // Free old harray
    g_free(self->harray);

    // Count entries
    int count = 0;
    GwHistEnt *ent = &self->head;
    while (ent != NULL) {
        count++;
        ent = ent->next;
    }

    self->numhist = count;

    if (count == 0) {
        self->harray = NULL;
        return;
    }

    // Allocate new harray
    self->harray = g_new(GwHistEnt *, count);

    // Fill harray
    ent = &self->head;
    for (int i = 0; i < count; i++) {
        self->harray[i] = ent;
        ent = ent->next;
    }
}
