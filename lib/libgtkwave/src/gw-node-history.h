#pragma once

#include <glib.h>
#include "gw-types.h"
#include "gw-hist-ent.h"

G_BEGIN_DECLS

typedef struct _GwNodeHistory GwNodeHistory;

struct _GwNodeHistory {
    GStaticRecMutex lock;
    gint            ref_count;

    GwHistEnt       head;
    GwHistEnt       *curr;
    GwHistEnt       **harray;
    int             numhist;

    GwTime          last_time;
    GwTime          last_time_raw;
};

GwNodeHistory* gw_node_history_new(void);
GwNodeHistory* gw_node_history_copy(GwNodeHistory *src);
void gw_node_history_ref(GwNodeHistory *history);
void gw_node_history_unref(GwNodeHistory *history);
void gw_node_history_append_entry(GwNodeHistory *history, GwHistEnt *entry);
void gw_node_history_regenerate_harray(GwNodeHistory *history);

G_END_DECLS