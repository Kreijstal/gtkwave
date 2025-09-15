#include "gw-interactive-loader.h"
#include <glib.h>

G_DEFINE_INTERFACE(GwInteractiveLoader, gw_interactive_loader, GW_TYPE_LOADER)

static void
gw_interactive_loader_default_init(GwInteractiveLoaderInterface *iface)
{
    /* Default interface initialization */
}

gboolean gw_interactive_loader_process_data(GwInteractiveLoader *loader, 
                                           const gchar *data, 
                                           gsize length)
{
    GwInteractiveLoaderInterface *iface;

    g_return_val_if_fail(GW_IS_INTERACTIVE_LOADER(loader), FALSE);
    g_return_val_if_fail(data != NULL, FALSE);

    iface = GW_INTERACTIVE_LOADER_GET_IFACE(loader);
    g_return_val_if_fail(iface->process_data != NULL, FALSE);

    return iface->process_data(loader, data, length);
}

gboolean gw_interactive_loader_is_complete(GwInteractiveLoader *loader)
{
    GwInteractiveLoaderInterface *iface;

    g_return_val_if_fail(GW_IS_INTERACTIVE_LOADER(loader), FALSE);

    iface = GW_INTERACTIVE_LOADER_GET_IFACE(loader);
    g_return_val_if_fail(iface->is_complete != NULL, FALSE);

    return iface->is_complete(loader);
}

void gw_interactive_loader_kick_processing(GwInteractiveLoader *loader)
{
    GwInteractiveLoaderInterface *iface;

    g_return_if_fail(GW_IS_INTERACTIVE_LOADER(loader));

    iface = GW_INTERACTIVE_LOADER_GET_IFACE(loader);
    g_return_if_fail(iface->kick_processing != NULL);

    iface->kick_processing(loader);
}

void gw_interactive_loader_cleanup(GwInteractiveLoader *loader)
{
    GwInteractiveLoaderInterface *iface;

    g_return_if_fail(GW_IS_INTERACTIVE_LOADER(loader));

    iface = GW_INTERACTIVE_LOADER_GET_IFACE(loader);
    g_return_if_fail(iface->cleanup != NULL);

    iface->cleanup(loader);
}