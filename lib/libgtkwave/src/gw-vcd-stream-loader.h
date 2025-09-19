#pragma once

#include <glib-object.h>
#include <gtkwave.h>

G_BEGIN_DECLS

#define GW_TYPE_VCD_STREAM_LOADER (gw_vcd_stream_loader_get_type())
G_DECLARE_FINAL_TYPE(GwVcdStreamLoader, gw_vcd_stream_loader, GW, VCD_STREAM_LOADER, GwLoader)

GwLoader *gw_vcd_stream_loader_new(void);

void gw_vcd_stream_loader_pump(GwVcdStreamLoader *self, const guint8 *buffer, gsize size, GError **error);
void gw_vcd_stream_loader_eof(GwVcdStreamLoader *self, GError **error);
GwDumpFile *gw_vcd_stream_loader_get_dump_file(GwVcdStreamLoader *self);

G_END_DECLS
