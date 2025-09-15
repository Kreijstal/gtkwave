#pragma once

#include "gw-interactive-loader.h"

G_BEGIN_DECLS

#define GW_TYPE_VCD_INTERACTIVE_LOADER (gw_vcd_interactive_loader_get_type())
G_DECLARE_FINAL_TYPE(GwVcdInteractiveLoader, gw_vcd_interactive_loader, GW, VCD_INTERACTIVE_LOADER, GObject)

/**
 * gw_vcd_interactive_loader_new:
 *
 * Creates a new VCD interactive loader.
 *
 * Returns: (transfer full): A newly allocated #GwVcdInteractiveLoader.
 * Free with g_object_unref().
 */
GwVcdInteractiveLoader *gw_vcd_interactive_loader_new(void);

G_END_DECLS