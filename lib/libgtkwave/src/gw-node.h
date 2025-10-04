#pragma once

#include "gw-types.h"
#include "gw-hist-ent.h"
#include "gw-vlist-writer.h"
#include "gw-vlist-reader.h"
#include "gw-node-history.h"

/* struct Node bitfield widths */
#define WAVE_VARXT_WIDTH (16)
#define WAVE_VARXT_MAX_ID ((1 << WAVE_VARXT_WIDTH) - 1)
#define WAVE_VARDT_WIDTH (6)
#define WAVE_VARDIR_WIDTH (3)
#define WAVE_VARTYPE_WIDTH (6)

// used when expanding atomic vectors
typedef struct
{
    GwNode **narray;
    int msb, lsb;
    int width;
} GwExpandInfo;

void gw_expand_info_free(GwExpandInfo *self);

struct _GwExpandReferences
{
    GwNode *parent; /* which atomic vec we expanded from */
    int parentbit; /* which bit from that atomic vec */
    int actual; /* bit number to be used in [] */
    int refcnt;
};

#ifdef WAVE_USE_STRUCT_PACKING
#pragma pack(push)
#pragma pack(1)
#endif

struct _GwNode
{
    GwExpandReferences *expansion; /* indicates which nptr this node was expanded from (if it was
                        expanded at all) and (when implemented) refcnts */
    char *nname; /* ascii name of node */
    GwHistEnt head; /* first entry in transition history */
    GwHistEnt *curr; /* ptr. to current history entry */

    GwHistEnt **harray; /* fill this in when we make a trace.. contains  */
    /*  a ptr to an array of histents for bsearching */
    union
    {
        GwFac *mvlfac; /* for use with mvlsim aets */
        GwVlist *mvlfac_vlist;
        GwVlistWriter *mvlfac_vlist_writer;
        GwVlistReader *mvlfac_vlist_reader; /* for live access to vlist data */
    } mv; /* anon union is a gcc extension so use mv instead.  using this union avoids crazy casting
             warnings */

    int msi, lsi; /* for 64-bit, more efficient than having as an external struct ExtNode*/

    int numhist; /* number of elements in the harray */

    GwTime last_time; /* time of last transition for delta calculation */
    GwTime last_time_raw; /* raw time value (without global offset) for delta calculation */

    // Thread-safe snapshot support
    gpointer active_history; /* Atomic pointer to GwNodeHistory snapshot. When non-NULL, this takes
                                precedence over direct field access */

    unsigned varxt : WAVE_VARXT_WIDTH; /* reference inside subvar_pnt[] */
    unsigned vardt : WAVE_VARDT_WIDTH; /* see nodeVarDataType, this is an internal value */
    unsigned vardir : WAVE_VARDIR_WIDTH; /* see nodeVarDir, this is an internal value (currently
                                            used only by extload and FST) */
    unsigned vartype : WAVE_VARTYPE_WIDTH; /* see nodeVarType, this is an internal value */

    unsigned extvals : 1; /* was formerly a pointer to ExtNode "ext", now simply a flag */
};

#ifdef WAVE_USE_STRUCT_PACKING
#pragma pack(pop)
#endif

GwExpandInfo *gw_node_expand(GwNode *self);

/**
 * gw_node_create_history_snapshot:
 * @node: A GwNode
 *
 * Creates a new GwNodeHistory snapshot from the node's current state.
 * The snapshot shares the node's history entries (doesn't deep copy them).
 * The snapshot gets a freshly generated harray that is consistent with the
 * current linked list.
 *
 * Returns: (transfer full): A new GwNodeHistory snapshot with refcount=1
 */
GwNodeHistory *gw_node_create_history_snapshot(GwNode *node);

/**
 * gw_node_get_history_snapshot:
 * @node: A GwNode
 *
 * Atomically acquires a reference to the node's history snapshot.
 * If no snapshot exists, returns NULL.
 * The caller MUST call gw_node_history_unref() when done.
 *
 * Returns: (transfer full) (nullable): A GwNodeHistory snapshot with incremented refcount
 */
GwNodeHistory *gw_node_get_history_snapshot(GwNode *node);

/**
 * gw_node_publish_new_history:
 * @node: A GwNode
 * @new_history: (transfer none): The new history snapshot to publish
 *
 * Atomically publishes a new history snapshot and returns the previous one.
 * The caller is responsible for unreffing the returned snapshot.
 *
 * Returns: (transfer full) (nullable): The previous history snapshot (if any)
 */
GwNodeHistory *gw_node_publish_new_history(GwNode *node, GwNodeHistory *new_history);
