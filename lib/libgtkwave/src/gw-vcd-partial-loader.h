#pragma once

#include <glib-object.h>
#include <gtkwave.h>
#include "gw-vcd-loader.h"

G_BEGIN_DECLS

#define GW_TYPE_VCD_PARTIAL_LOADER (gw_vcd_partial_loader_get_type())
G_DECLARE_FINAL_TYPE(GwVcdPartialLoader, gw_vcd_partial_loader, GW, VCD_PARTIAL_LOADER, GwVcdLoader)

GwLoader *gw_vcd_partial_loader_new(void);

/* Partial VCD specific functions */
void gw_vcd_partial_loader_kick(GwVcdPartialLoader *self);
void gw_vcd_partial_loader_mark_and_sweep(GwVcdPartialLoader *self, gboolean mandclear);

/* Shared memory configuration */
void gw_vcd_partial_loader_set_shared_memory_id(GwVcdPartialLoader *self, guint shmid);
guint gw_vcd_partial_loader_get_shared_memory_id(GwVcdPartialLoader *self);

/* Streaming control */
void gw_vcd_partial_loader_set_streaming_enabled(GwVcdPartialLoader *self, gboolean enabled);
gboolean gw_vcd_partial_loader_is_streaming_enabled(GwVcdPartialLoader *self);

/* Callback for real-time updates */
typedef void (*GwVcdPartialUpdateCallback)(GwVcdPartialLoader *self, GwTime current_time, gpointer user_data);
void gw_vcd_partial_loader_set_update_callback(GwVcdPartialLoader *self, 
                                              GwVcdPartialUpdateCallback callback,
                                              gpointer user_data);
GwVcdPartialUpdateCallback gw_vcd_partial_loader_get_update_callback(GwVcdPartialLoader *self);
gpointer gw_vcd_partial_loader_get_update_callback_data(GwVcdPartialLoader *self);

G_END_DECLS