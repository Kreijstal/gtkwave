#pragma once

#include <glib.h>
#include "gw-hist-ent.h"
#include "gw-bit.h"
#include "gw-vlist.h"

G_BEGIN_DECLS

typedef struct _GwNodeHistory GwNodeHistory;

struct _GwNodeHistory {
    GRecMutex lock;
    gint            ref_count;
    GwHistEnt       head;
    GwHistEnt       *curr;
    GwHistEnt       **harray;
    int             numhist;
    GwVlist         *vlist;
};

GwNodeHistory* gw_node_history_new(void);
GwNodeHistory* gw_node_history_copy(const GwNodeHistory *src);
void gw_node_history_copy_from(GwNodeHistory *dest, const GwNodeHistory *src);
void gw_node_history_ref(GwNodeHistory *history);
void gw_node_history_unref(GwNodeHistory *history);
void gw_node_history_append_entry(GwNodeHistory *history, GwHistEnt *hent);
void gw_node_history_regenerate_harray(GwNodeHistory *history);

G_END_DECLS