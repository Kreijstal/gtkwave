#!/usr/bin/env bash
#
# Demonstration script showing how to run the commands from the issue
# with proper accessibility support. This version wraps the demo execution
# with `run_with_accessibility.sh` which will detect headless environments
# and start Xvfb / D-Bus / AT-SPI as needed.
#
# If `run_with_accessibility.sh` is not present, it falls back to the
# older `setup_test_env.sh` usage.
#

set -euo pipefail

cd "$(dirname "$0")/.."

echo "====================================================="
echo "GTKWave Accessibility Test - Wrapped Demo Workflow"
echo "====================================================="
echo ""

# Step 1: Compile
echo "Step 1: Compiling GTKWave..."
meson compile -C builddir
echo "✓ Compilation complete"
echo ""

# Helper to run the demo under a provided launcher (either run_with_accessibility.sh
# or setup_test_env.sh). The launcher is expected to accept a command to run.
_run_demo_via_launcher() {
    local launcher="$1"

    if [ ! -x "$launcher" ]; then
        echo "ERROR: Launcher not found or not executable: $launcher"
        return 2
    fi

    # Feed the demo steps to the launcher by piping a small shell script.
    # The launcher will run `bash -s` (read script from stdin) so the inner script
    # executes in the prepared accessibility environment.
    cat <<'INNER' | "$launcher" bash -s --
# Inside accessibility-prepared environment (DISPLAY, DBUS, AT-SPI should be available)
set -euo pipefail

echo "Loading meson devenv..."
# meson devenv prints shell assignments; use process substitution to source them.
# If meson devenv is not present, this will fail, which is acceptable here.
if command -v meson >/dev/null 2>&1; then
    # shellcheck disable=SC1091
    source <(meson devenv -C builddir --dump)
else
    echo "Warning: meson not found in PATH; proceeding without meson devenv."
fi

echo "Environment ready:"
echo "  - DISPLAY=${DISPLAY:-<unset>}"
echo "  - DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-<unset>}"
echo ""

# Start awesome.py in background (as specified in issue)
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

# Cleanup background helper
echo ""
echo "Cleaning up background processes..."
kill "${AWESOME_PID}" 2>/dev/null || true
wait "${AWESOME_PID}" 2>/dev/null || true

echo ""
if [ "${EXIT_CODE:-0}" -eq 0 ]; then
    echo "====================================================="
    echo "✅ ALL TESTS COMPLETED SUCCESSFULLY - NO SEGFAULTS!"
    echo "====================================================="
else
    echo "====================================================="
    echo "❌ Tests failed with exit code: ${EXIT_CODE}"
    echo "====================================================="
fi

exit "${EXIT_CODE}"
INNER
}

# Prefer using the new run_with_accessibility wrapper if available.
LAUNCHER="./scripts/run_with_accessibility.sh"
if [ -x "${LAUNCHER}" ]; then
    echo "Using ${LAUNCHER} to provide Xvfb / D-Bus / AT-SPI when needed..."
    _run_demo_via_launcher "${LAUNCHER}"
    EXIT_CODE=$?
else
    echo "Notice: ${LAUNCHER} not found. Falling back to setup_test_env.sh (older behavior)."
    # Use the older setup script which performs its own DBus/AT-SPI setup.
    if [ -x "./scripts/setup_test_env.sh" ]; then
        ./scripts/setup_test_env.sh bash -c '
            echo "Loading meson devenv..."
            if command -v meson >/dev/null 2>&1; then
                source <(meson devenv -C builddir --dump)
            fi

            echo "Starting awesome.py in background..."
            /usr/bin/python3 scripts/awesome.py &
            AWESOME_PID=$!
            sleep 2

            echo "Running stream_to_gtkwave.py..."
            /usr/bin/python3 scripts/stream_to_gtkwave.py
            EXIT_CODE=$?

            echo "Cleaning up..."
            kill $AWESOME_PID 2>/dev/null || true

            exit $EXIT_CODE
        '
        EXIT_CODE=$?
    else
        echo "ERROR: No launcher available (neither ${LAUNCHER} nor ./scripts/setup_test_env.sh found)."
        EXIT_CODE=3
    fi
fi

echo ""
echo "Done! (exit code: ${EXIT_CODE})"
exit "${EXIT_CODE}"
