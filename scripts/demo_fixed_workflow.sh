#!/bin/bash
#
# Demonstration script showing how to run the commands from the issue
# with proper AT-SPI setup to avoid segfaults
#
# Original commands (that caused segfaults):
#   meson compile -C builddir
#   meson devenv -C builddir
#   python scripts/awesome.py &
#   python scripts/stream_to_gtkwave.py
#

set -e

cd "$(dirname "$0")/.."

echo "====================================================="
echo "GTKWave Accessibility Test - Fixed Version"
echo "====================================================="
echo ""

# Step 1: Compile
echo "Step 1: Compiling GTKWave..."
meson compile -C builddir
echo "✓ Compilation complete"
echo ""

# Step 2: Ensure Xvfb is running
echo "Step 2: Checking X server..."
export DISPLAY=${DISPLAY:-:99}
if ! ps aux | grep -q "[X]vfb :99"; then
    echo "Starting Xvfb on display :99..."
    Xvfb :99 -screen 0 1024x768x24 -ac +extension GLX +render -noreset &
    XVFB_PID=$!
    sleep 2
    echo "✓ Xvfb started (PID: $XVFB_PID)"
else
    echo "✓ Xvfb already running"
fi
echo ""

# Step 3: Set up environment and run tests with AT-SPI support
echo "Step 3: Running tests with AT-SPI infrastructure..."
echo "(This fixes the segfaults by providing proper D-Bus and AT-SPI setup)"
echo ""

./scripts/setup_test_env.sh bash -c '
    # Set up meson development environment
    echo "Loading meson devenv..."
    source <(meson devenv -C builddir --dump)
    
    echo "Environment ready:"
    echo "  - DISPLAY=$DISPLAY"
    echo "  - DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS"
    echo "  - PATH includes: builddir/src and builddir/src/helpers"
    echo ""
    
    # Run awesome.py in background (as specified in issue)
    echo "Starting awesome.py in background..."
    /usr/bin/python3 scripts/awesome.py &
    AWESOME_PID=$!
    echo "✓ awesome.py running (PID: $AWESOME_PID)"
    echo ""
    
    # Give it a moment to initialize
    sleep 2
    
    # Run stream_to_gtkwave.py (as specified in issue)
    echo "Running stream_to_gtkwave.py..."
    /usr/bin/python3 scripts/stream_to_gtkwave.py
    EXIT_CODE=$?
    
    # Cleanup
    echo ""
    echo "Cleaning up..."
    kill $AWESOME_PID 2>/dev/null || true
    
    echo ""
    if [ $EXIT_CODE -eq 0 ]; then
        echo "====================================================="
        echo "✅ ALL TESTS COMPLETED SUCCESSFULLY - NO SEGFAULTS!"
        echo "====================================================="
    else
        echo "====================================================="
        echo "❌ Tests failed with exit code: $EXIT_CODE"
        echo "====================================================="
    fi
    
    exit $EXIT_CODE
'

echo ""
echo "Done!"
