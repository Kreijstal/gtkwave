#pragma once

#include "gtkwave.h"
#include "gw-node.h"
#include "gw-time.h"

G_BEGIN_DECLS

#define GW_TYPE_VCD_FILE (gw_vcd_file_get_type())
G_DECLARE_FINAL_TYPE(GwVcdFile, gw_vcd_file, GW, VCD_FILE, GwDumpFile)

void gw_vcd_file_add_histent_to_node(GwVcdFile *self, GwNode *n, GwTime tim, GwBit bit);

G_END_DECLS
