#pragma once

#include <glib-object.h>
#include "gw-named-markers.h"
#include "gw-marker.h"
#include "gw-hierpack.h"
#include "gw-interactive-loader.h"
#include "gw-shared-memory.h"

G_BEGIN_DECLS

#define GW_TYPE_PROJECT (gw_project_get_type())
G_DECLARE_FINAL_TYPE(GwProject, gw_project, GW, PROJECT, GObject)

GwProject *gw_project_new(void);

GwMarker *gw_project_get_cursor(GwProject *self);
GwMarker *gw_project_get_primary_marker(GwProject *self);
GwMarker *gw_project_get_baseline_marker(GwProject *self);
GwMarker *gw_project_get_ghost_marker(GwProject *self);

GwNamedMarkers *gw_project_get_named_markers(GwProject *self);

/* Hierpack context accessors */
GwHierpackContext *gw_project_get_hierpack_context(GwProject *self);
void gw_project_set_hierpack_context(GwProject *self, GwHierpackContext *context);

/* Interactive loader accessors */
GwInteractiveLoader *gw_project_get_interactive_loader(GwProject *self);
void gw_project_set_interactive_loader(GwProject *self, GwInteractiveLoader *loader);

/* Shared memory accessors */
GwSharedMemory *gw_project_get_shared_memory(GwProject *self);
void gw_project_set_shared_memory(GwProject *self, GwSharedMemory *shm);

G_END_DECLS
