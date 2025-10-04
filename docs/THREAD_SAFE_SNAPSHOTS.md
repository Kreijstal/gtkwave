# Thread-Safe Snapshot Architecture for GTKWave VCD Partial Loader

## Problem Statement

The original implementation had a critical data race between:

**Producer (VCD Loader)**: `gw_vcd_partial_loader_get_dump_file()`
- Appends history entries to `GwNode` linked list
- Increments `numhist` counter
- Does NOT update the `harray` (binary search array)

**Consumer (GUI Renderer)**: `bsearch_node()`
- Uses `harray` for binary search
- Uses `numhist` to determine array bounds

**Race Condition**: The GUI could read an outdated `harray` with an updated `numhist`, causing binary search to read past the allocated array bounds, resulting in crashes.

## Solution: Read-Copy-Update (RCU) Pattern

We implemented a thread-safe snapshot architecture using the RCU pattern, which allows:
- Lock-free reads for the GUI (high performance)
- Safe updates from the VCD loader
- Proper memory management with reference counting

## Architecture Overview

### Core Components

#### 1. GwNodeHistory (New)
Location: `lib/libgtkwave/src/gw-node-history.{h,c}`

An opaque structure that encapsulates node history data:
- `head`: First entry in transition history (inline struct)
- `curr`: Pointer to current/last entry
- `harray`: Binary search array
- `numhist`: Number of history entries
- `ref_count`: Atomic reference counter for RCU

Key characteristics:
- Reference counted for safe memory management
- Snapshots share history entries (no deep copy needed)
- Each snapshot has its own `harray` for consistency
- Freed only when reference count reaches zero

#### 2. GwNode Enhancement
Location: `lib/libgtkwave/src/gw-node.{h,c}`

Added new field:
```c
gpointer active_history;  // Atomic pointer to GwNodeHistory snapshot
```

New API functions:
- `gw_node_create_history_snapshot()`: Creates a snapshot from node's current state
- `gw_node_get_history_snapshot()`: Atomically acquires a reference to the snapshot
- `gw_node_publish_new_history()`: Atomically publishes a new snapshot

### Data Flow

#### Producer Side (VCD Partial Loader)

When new VCD data arrives:

1. **Append to linked list**: Add new `GwHistEnt` entries directly to the node's linked list
2. **Create snapshot**: Call `gw_node_create_history_snapshot(node)`
   - Creates new `GwNodeHistory` with ref_count = 1
   - Shares the node's linked list (no copying)
   - Regenerates `harray` to be consistent with current `numhist`
3. **Publish atomically**: Call `gw_node_publish_new_history(node, new_snapshot)`
   - Uses `g_atomic_pointer_exchange()` for thread-safe swap
   - Returns old snapshot (if any)
4. **Release old**: Call `gw_node_history_unref(old_snapshot)`
   - Decrements ref_count
   - Frees snapshot when count reaches zero

#### Consumer Side (GUI Rendering)

When rendering waveforms:

1. **Acquire snapshot**: Call `gw_node_get_history_snapshot(node)`
   - Atomically gets the current snapshot
   - Increments ref_count
   - Returns NULL if no snapshot exists (falls back to direct access)
2. **Use snapshot**: Read `harray` and `numhist` from snapshot
   - Guaranteed to be consistent
   - No locks needed during read
3. **Release snapshot**: Call `gw_node_history_unref(snapshot)`
   - Decrements ref_count
   - Allows old snapshots to be freed

### Modified Functions

#### lib/libgtkwave/src/gw-vcd-partial-loader.c

In `gw_vcd_partial_loader_get_dump_file()`, after appending new history entries:

```c
// Create and publish thread-safe snapshot
GwNodeHistory *new_snapshot = gw_node_create_history_snapshot(node);
GwNodeHistory *old_snapshot = gw_node_publish_new_history(node, new_snapshot);
if (old_snapshot != NULL) {
    gw_node_history_unref(old_snapshot);
}
```

#### src/bsearch.c

In `bsearch_node()`:

```c
GwHistEnt *bsearch_node(GwNode *n, GwTime key)
{
    // Try to use thread-safe snapshot if available
    GwNodeHistory *history = gw_node_get_history_snapshot(n);
    
    GwHistEnt **harray;
    int numhist;
    
    if (history != NULL) {
        // Use snapshot (thread-safe)
        harray = gw_node_history_get_harray(history);
        numhist = gw_node_history_get_numhist(history);
    } else {
        // Fall back to direct access (legacy behavior)
        harray = n->harray;
        numhist = n->numhist;
    }
    
    // ... perform binary search ...
    
    // Release the snapshot
    if (history != NULL) {
        gw_node_history_unref(history);
    }
    
    return result;
}
```

## Key Design Decisions

### 1. Shared History Entries

**Decision**: Snapshots share `GwHistEnt` objects rather than deep-copying them.

**Rationale**:
- History entries are immutable once created
- Deep copying is problematic (vectors without size metadata, union types)
- Sharing entries is safe with proper reference counting
- Significantly better performance (no memory allocation/copying)

**Implementation**: 
- `GwNodeHistory` doesn't free the linked list entries on unref
- Entries are owned by `GwNode` and freed only when node is destroyed
- Each snapshot gets its own `harray` (cheap to rebuild)

### 2. Backward Compatibility

**Decision**: Fall back to direct field access if no snapshot exists.

**Rationale**:
- Non-partial VCD loaders don't use snapshots
- FST, GHW, and other loaders continue to work unchanged
- Gradual migration path

**Implementation**:
- All snapshot accessors return NULL for non-snapshot nodes
- Consumer code checks for NULL and uses fallback path

### 3. Atomic Operations

**Decision**: Use GLib's atomic pointer operations.

**Rationale**:
- Portable across platforms
- Well-tested implementation
- No external dependencies
- Minimal performance overhead

**Implementation**:
- `g_atomic_pointer_get()` for snapshot acquisition
- `g_atomic_pointer_exchange()` for snapshot publication
- `g_atomic_int_inc()` / `g_atomic_int_dec_and_test()` for ref counting

## Performance Characteristics

### Memory Overhead
- One `GwNodeHistory` struct per node with active snapshot (~40 bytes)
- One `harray` per snapshot (8 bytes × numhist)
- No deep copying of history entries

### Time Complexity
- **Snapshot creation**: O(numhist) to regenerate harray
- **Snapshot acquisition**: O(1) atomic operation + refcount increment
- **Snapshot release**: O(1) atomic operation + refcount decrement
- **Binary search**: Unchanged O(log numhist)

### Scalability
- Supports thousands of signals with millions of transitions
- No global locks - each node has independent snapshot
- GUI rendering remains lock-free

## Thread Safety Guarantees

1. **No data races**: Producer and consumer never modify the same memory
2. **Consistent reads**: GUI always sees consistent harray/numhist pairs
3. **Memory safety**: Reference counting prevents use-after-free
4. **Progress guarantee**: Lock-free reads ensure GUI never blocks

## Testing

All existing tests pass:
- `test-gw-node-history`: Basic snapshot functionality
- `test-gw-vcd-partial-loader-*`: VCD partial loading scenarios
- All other GTKWave tests: Regression testing

## Future Work

Potential enhancements:
1. Apply snapshot pattern to other history access points
2. Add memory pool for `GwNodeHistory` to reduce allocations
3. Add performance metrics/monitoring
4. Consider snapshot batching for multiple signals

## References

- Read-Copy-Update (RCU): https://en.wikipedia.org/wiki/Read-copy-update
- GLib Atomic Operations: https://docs.gtk.org/glib/atomic-operations.html
