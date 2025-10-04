#!/bin/bash
#
# Wrapper script to run GTKWave tests with proper accessibility support in Xvfb
#
# This script sets up the necessary D-Bus and AT-SPI infrastructure required
# for pyatspi-based tests to work properly in a headless environment.
#

set -e

# Set display
export DISPLAY=${DISPLAY:-:99}

# Ensure we have a session D-Bus
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ]; then
    echo "Starting D-Bus session..."
    eval $(dbus-launch --sh-syntax)
    export DBUS_SESSION_BUS_ADDRESS
    DBUS_STARTED=1
fi

echo "D-Bus session address: $DBUS_SESSION_BUS_ADDRESS"

# Start AT-SPI registry
echo "Starting AT-SPI registry..."
/usr/libexec/at-spi2-registryd &
ATSPI_PID=$!
sleep 1

# Verify AT-SPI is accessible
if ! ps -p $ATSPI_PID > /dev/null; then
    echo "Warning: AT-SPI registry failed to start"
fi

# Run the command passed as arguments
"$@"
EXIT_CODE=$?

# Cleanup
kill $ATSPI_PID 2>/dev/null || true
if [ -n "$DBUS_STARTED" ]; then
    kill $DBUS_SESSION_BUS_PID 2>/dev/null || true
fi

exit $EXIT_CODE
