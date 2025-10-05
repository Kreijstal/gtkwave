# Group Expansion Fix - Verification Guide

## How to Verify the Fix

### Quick Verification
1. Build GTKWave with the changes:
   ```bash
   meson compile -C builddir
   ```

2. Run with debug output enabled (if built with DEBUG defined):
   ```bash
   ./builddir/src/gtkwave -I <streaming_vcd_file>
   ```

3. Expand a vector signal into its bit components

4. Observe debug output like:
   ```
   bsearch_node: Rebuilding expanded child 'signal[0]' (bit 0) from parent 'signal[7:0]' (numhist=150)
     Built 45 history entries for child from parent's 150 entries
   ```

5. Verify child signals update as new VCD data arrives

### Detailed Test with demo_fixed_workflow.sh

The demo script tests the exact scenario from the issue:

```bash
cd /home/runner/work/gtkwave/gtkwave
scripts/demo_fixed_workflow.sh
```

**Expected behavior:**
- GTKWave window opens
- VCD data streams in real-time
- `mysim` group can be expanded
- `sine_wave` signal appears and updates continuously
- After "Toggle Group Open|Close" action, child signals in the group should **continue updating**

**Previous behavior (before fix):**
- Child signals would freeze at their values from the expansion moment
- Only the parent/group signal would continue updating

### Code Flow Verification

1. **At expansion time** (when user expands a vector):
   - `menu_expand()` → `expand_trace()` → `gw_node_expand()`
   - Child nodes created with `expansion->parent` pointing to parent
   - **NO history copied** - children start with minimal history

2. **During rendering** (every frame update):
   - `draw_hptr_trace()` → `bsearch_node(child)`
   - **bsearch_node detects expanded node**
   - Frees old child history
   - Rebuilds child history from parent's current data
   - Returns appropriate history entry for rendering

3. **When streaming data arrives**:
   - `add_histent_vector()` updates parent node only
   - Parent's `harray` is invalidated (set to NULL)
   - Next render: child nodes see updated parent data via rebuild

### Debug Output Examples

With DEBUG enabled, you should see:
```
bsearch_node: Rebuilding expanded child 'mysim.sine_wave[7]' (bit 7) from parent 'mysim.sine_wave[7:0]' (numhist=234)
  Built 89 history entries for child from parent's 234 entries
```

This confirms:
- Child node is being rebuilt dynamically
- Parent has growing history (234 entries in this example)
- Child history is compressed (89 vs 234) by skipping unchanged values

### Known Limitations

1. **Performance**: Child history is rebuilt on every bsearch_node call
   - This is intentional for correctness
   - Future optimization: cache and invalidate on parent update

2. **Memory**: Temporary overhead during rebuild
   - Old child history freed before new one built
   - Peak memory = old_child_history + new_child_history (briefly)

3. **Not for non-streaming files**: Fix works for all files, but the problem only occurs with streaming

## Success Criteria

✅ Child signals update in real-time during streaming
✅ No crashes or memory corruption
✅ Waveform values match parent's vector data
✅ Toggle expand/collapse works correctly
✅ Debug output confirms dynamic rebuilding (if DEBUG enabled)
