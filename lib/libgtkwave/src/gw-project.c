#include "gw-project.h"

/**
 * GwProject:
 *
 * A GTKWave project.
 */

struct _GwProject
{
    GObject parent_instance;

    GwMarker *cursor;
    GwMarker *primary_marker;
    GwMarker *baseline_marker;
    GwMarker *ghost_marker;
    GwNamedMarkers *named_markers;
    
    /* Phase 2 additions */
    GwHierpackContext *hierpack_context;
    GwInteractiveLoader *interactive_loader;
    GwSharedMemory *shared_memory;
};

G_DEFINE_TYPE(GwProject, gw_project, G_TYPE_OBJECT)

enum
{
    PROP_CURSOR = 1,
    PROP_PRIMARY_MARKER,
    PROP_BASELINE_MARKER,
    PROP_GHOST_MARKER,
    PROP_NAMED_MARKERS,
    PROP_HIERPACK_CONTEXT,
    PROP_INTERACTIVE_LOADER,
    PROP_SHARED_MEMORY,
    N_PROPERTIES,
};

enum
{
    UNNAMED_MARKER_CHANGED,
    N_SIGNALS,
};

static GParamSpec *properties[N_PROPERTIES];
static guint signals[N_SIGNALS];

static void on_marker_notify(GwMarker *marker, GParamSpec *pspec, GwProject *project)
{
    (void)pspec;
    (void)marker;

    g_signal_emit(project, signals[UNNAMED_MARKER_CHANGED], 0);
}

static void gw_project_dispose(GObject *object)
{
    GwProject *self = GW_PROJECT(object);

    g_clear_object(&self->cursor);
    g_clear_object(&self->primary_marker);
    g_clear_object(&self->baseline_marker);
    g_clear_object(&self->ghost_marker);
    g_clear_object(&self->named_markers);
    
    /* Phase 2 additions */
    if (self->hierpack_context != NULL) {
        gw_hierpack_context_free(self->hierpack_context);
        self->hierpack_context = NULL;
    }
    g_clear_object(&self->interactive_loader);
    if (self->shared_memory != NULL) {
        gw_shared_memory_free(self->shared_memory);
        self->shared_memory = NULL;
    }

    G_OBJECT_CLASS(gw_project_parent_class)->dispose(object);
}

static void gw_project_get_property(GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
    GwProject *self = GW_PROJECT(object);

    switch (property_id) {
        case PROP_CURSOR:
            g_value_set_object(value, gw_project_get_cursor(self));
            break;

        case PROP_PRIMARY_MARKER:
            g_value_set_object(value, gw_project_get_primary_marker(self));
            break;

        case PROP_BASELINE_MARKER:
            g_value_set_object(value, gw_project_get_baseline_marker(self));
            break;

        case PROP_GHOST_MARKER:
            g_value_set_object(value, gw_project_get_ghost_marker(self));
            break;

        case PROP_NAMED_MARKERS:
            g_value_set_object(value, gw_project_get_named_markers(self));
            break;
            
        case PROP_HIERPACK_CONTEXT:
            g_value_set_pointer(value, gw_project_get_hierpack_context(self));
            break;
            
        case PROP_INTERACTIVE_LOADER:
            g_value_set_object(value, gw_project_get_interactive_loader(self));
            break;
            
        case PROP_SHARED_MEMORY:
            g_value_set_pointer(value, gw_project_get_shared_memory(self));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void gw_project_class_init(GwProjectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = gw_project_dispose;
    object_class->get_property = gw_project_get_property;

    /**
     * GwProject:cursor:
     *
     * The cursor.
     */
    properties[PROP_CURSOR] = g_param_spec_object("cursor",
                                                  "Cursor",
                                                  "The cursor",
                                                  GW_TYPE_MARKER,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * GwProject:primary-marker:
     *
     * The primary marker.
     */
    properties[PROP_PRIMARY_MARKER] =
        g_param_spec_object("primary-marker",
                            "Primary marker",
                            "The primary marker",
                            GW_TYPE_MARKER,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * GwProject:baseline-marker:
     *
     * The baseline marker.
     */
    properties[PROP_BASELINE_MARKER] =
        g_param_spec_object("baseline-marker",
                            "Baseline marker",
                            "The baseline marker",
                            GW_TYPE_MARKER,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * GwProject:ghost-marker:
     *
     * The ghost marker.
     */
    properties[PROP_GHOST_MARKER] = g_param_spec_object("ghost-marker",
                                                        "Ghost marker",
                                                        "The ghost marker",
                                                        GW_TYPE_MARKER,
                                                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * GwProject:named-markers:
     *
     * The named markers.
     */
    properties[PROP_NAMED_MARKERS] = g_param_spec_object("named-markers",
                                                         "Named markers",
                                                         "The named markers",
                                                         GW_TYPE_NAMED_MARKERS,
                                                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

   /**
    * GwProject:hierpack-context:
    *
    * The hierarchy compression context.
    */
   properties[PROP_HIERPACK_CONTEXT] = g_param_spec_pointer("hierpack-context",
                                                           "Hierpack context",
                                                           "The hierarchy compression context",
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

   /**
    * GwProject:interactive-loader:
    *
    * The interactive loader.
    */
   properties[PROP_INTERACTIVE_LOADER] = g_param_spec_object("interactive-loader",
                                                            "Interactive loader",
                                                            "The interactive loader",
                                                            GW_TYPE_INTERACTIVE_LOADER,
                                                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

   /**
    * GwProject:shared-memory:
    *
    * The shared memory segment.
    */
   properties[PROP_SHARED_MEMORY] = g_param_spec_pointer("shared-memory",
                                                        "Shared memory",
                                                        "The shared memory segment",
                                                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * GwProject::unnamed-marker-changed:
     * @self: The project.
     *
     * The "unnamed-marker-changed" signal is emitted if any unnamed marker has changed.
     */
    signals[UNNAMED_MARKER_CHANGED] = g_signal_new("unnamed-marker-changed",
                                                   G_TYPE_FROM_CLASS(klass),
                                                   G_SIGNAL_RUN_LAST,
                                                   0,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   G_TYPE_NONE,
                                                   0);

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);
}

static void gw_project_init(GwProject *self)
{
    self->cursor = gw_marker_new("Cursor");
    self->primary_marker = gw_marker_new("Primary marker");
    self->baseline_marker = gw_marker_new("Baseline marker");
    self->ghost_marker = gw_marker_new("Ghost marker");

    g_signal_connect(self->primary_marker, "notify", G_CALLBACK(on_marker_notify), self);
    g_signal_connect(self->baseline_marker, "notify", G_CALLBACK(on_marker_notify), self);
    g_signal_connect(self->ghost_marker, "notify", G_CALLBACK(on_marker_notify), self);

    self->named_markers = gw_named_markers_new(26); // TODO: support manymarkers
}

/**
 * gw_project_new:
 *
 * Creates a new project.
 *
 * Returns: (transfer full): The project.
 */
GwProject *gw_project_new(void)
{
    return g_object_new(GW_TYPE_PROJECT, NULL);
}

/**
 * gw_project_get_cursor:
 * @self: A #GwProject.
 *
 * Returns the cursor.
 *
 * Returns: (transfer none): The cursor.
 */
GwMarker *gw_project_get_cursor(GwProject *self)
{
    g_return_val_if_fail(GW_IS_PROJECT(self), NULL);

    return self->cursor;
}

/**
 * gw_project_get_primary_marker:
 * @self: A #GwProject.
 *
 * Returns the primary marker.
 *
 * Returns: (transfer none): The primary marker.
 */
GwMarker *gw_project_get_primary_marker(GwProject *self)
{
    g_return_val_if_fail(GW_IS_PROJECT(self), NULL);

    return self->primary_marker;
}

/**
 * gw_project_get_baseline_marker:
 * @self: A #GwProject.
 *
 * Returns the baseline marker.
 *
 * Returns: (transfer none): The baseline marker.
 */
GwMarker *gw_project_get_baseline_marker(GwProject *self)
{
    g_return_val_if_fail(GW_IS_PROJECT(self), NULL);

    return self->baseline_marker;
}

/**
 * gw_project_get_ghost_marker:
 * @self: A #GwProject.
 *
 * Returns the ghost marker.
 *
 * Returns: (transfer none): The ghost marker.
 */
GwMarker *gw_project_get_ghost_marker(GwProject *self)
{
    g_return_val_if_fail(GW_IS_PROJECT(self), NULL);

    return self->ghost_marker;
}

/**
 * gw_project_get_named_markers:
 * @self: A #GwProject.
 *
 * Returns the named markers.
 *
 * Returns: (transfer none): The named markers.
 */
GwNamedMarkers *gw_project_get_named_markers(GwProject *self)
{
    g_return_val_if_fail(GW_IS_PROJECT(self), NULL);

    return self->named_markers;
}

/**
 * gw_project_get_hierpack_context:
 * @self: A #GwProject.
 *
 * Returns the hierarchy compression context.
 *
 * Returns: (transfer none): The hierarchy compression context.
 */
GwHierpackContext *gw_project_get_hierpack_context(GwProject *self)
{
    g_return_val_if_fail(GW_IS_PROJECT(self), NULL);
    return self->hierpack_context;
}

/**
 * gw_project_set_hierpack_context:
 * @self: A #GwProject.
 * @context: (transfer full): The hierarchy compression context.
 *
 * Sets the hierarchy compression context.
 */
void gw_project_set_hierpack_context(GwProject *self, GwHierpackContext *context)
{
    g_return_if_fail(GW_IS_PROJECT(self));
    
    if (self->hierpack_context != NULL) {
        gw_hierpack_context_free(self->hierpack_context);
    }
    self->hierpack_context = context;
}

/**
 * gw_project_get_interactive_loader:
 * @self: A #GwProject.
 *
 * Returns the interactive loader.
 *
 * Returns: (transfer none): The interactive loader.
 */
GwInteractiveLoader *gw_project_get_interactive_loader(GwProject *self)
{
    g_return_val_if_fail(GW_IS_PROJECT(self), NULL);
    return self->interactive_loader;
}

/**
 * gw_project_set_interactive_loader:
 * @self: A #GwProject.
 * @loader: (transfer full): The interactive loader.
 *
 * Sets the interactive loader.
 */
void gw_project_set_interactive_loader(GwProject *self, GwInteractiveLoader *loader)
{
    g_return_if_fail(GW_IS_PROJECT(self));
    g_set_object(&self->interactive_loader, loader);
}

/**
 * gw_project_get_shared_memory:
 * @self: A #GwProject.
 *
 * Returns the shared memory segment.
 *
 * Returns: (transfer none): The shared memory segment.
 */
GwSharedMemory *gw_project_get_shared_memory(GwProject *self)
{
    g_return_val_if_fail(GW_IS_PROJECT(self), NULL);
    return self->shared_memory;
}

/**
 * gw_project_set_shared_memory:
 * @self: A #GwProject.
 * @shm: (transfer full): The shared memory segment.
 *
 * Sets the shared memory segment.
 */
void gw_project_set_shared_memory(GwProject *self, GwSharedMemory *shm)
{
    g_return_if_fail(GW_IS_PROJECT(self));
    
    if (self->shared_memory != NULL) {
        gw_shared_memory_free(self->shared_memory);
    }
    self->shared_memory = shm;
}
