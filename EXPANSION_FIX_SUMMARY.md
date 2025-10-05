# Fix for Group Expansion Freeze During Streaming

## Problem
When using GTKWave with streaming VCD data, expanding a vector signal into its bit components causes the child signals to freeze and no longer update in real-time. The main signal continues to update, but the expanded bit signals remain stuck at their values from the moment of expansion.

## Root Cause
The original implementation in `gw_node_expand()` (lib/libgtkwave/src/gw-node.c) created independent child nodes by **copying** the parent's history at the time of expansion. These child nodes had their own history data structures that were completely separate from the parent.

When new data arrived via streaming:
1. The parent node's history was updated by `add_histent_vector()` 
2. The child nodes' histories remained unchanged (they were static copies)
3. The child signals displayed stale data from the expansion moment

## Solution
Modified the expansion mechanism to make child nodes **dynamically reference** their parent's data:

### 1. Changes to `gw_node_expand()` (lib/libgtkwave/src/gw-node.c)
- Child nodes are created with **minimal history** (just the head node)
- The expansion parent pointer is set, but **no history is copied**
- Old history copying code (lines 188-283) is disabled with `#if 0`
- Child nodes start with:
  - `head.time = -1`
  - `head.v.h_val = GW_BIT_X` 
  - `numhist = 0`
  - `harray = NULL`

### 2. Changes to `bsearch_node()` (src/bsearch.c)
Added special handling for expanded child nodes:

1. **Detection**: Check if `n->expansion && n->expansion->parent` is set
2. **Cleanup**: Free any existing child history to avoid stale data
3. **Parent sync**: Ensure parent's harray is built (recursive call if needed)
4. **History rebuild**: Iterate through parent's current history and extract the appropriate bit for each history entry
5. **Optimization**: Only create history entries when the bit value changes
6. **Result**: Child node has a fresh history built from parent's current data

This rebuild happens **every time** `bsearch_node()` is called on an expanded child node, ensuring perfect synchronization with parent updates.

## Performance Considerations
Rebuilding child history on every render could be expensive. However:
- GTKWave already rebuilds harray when NULL (e.g., after streaming updates)
- The rebuild only processes parent history entries where bit values change
- Binary search and linked list traversal remain efficient
- The tradeoff ensures correctness over micro-optimization

Future optimization: Track parent's numhist and only rebuild when it changes.

## Testing
The fix should be tested with:
1. Run `scripts/demo_fixed_workflow.sh` with ASAN
2. Expand a vector signal during VCD streaming
3. Verify child signals continue updating in real-time
4. Check that waveforms match expected values from the VCD data

## Files Modified
- `lib/libgtkwave/src/gw-node.c`: Skip history copying in `gw_node_expand()`
- `src/bsearch.c`: Add dynamic history rebuild for expanded nodes
