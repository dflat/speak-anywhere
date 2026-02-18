#!/bin/bash
set -e

# Define paths
BUILD_DIR="build"
INSTALL_DIR="$HOME/.local/bin"

# Create install dir if it doesn't exist
mkdir -p "$INSTALL_DIR"

# Restart systemd unit
echo "Stopping speak-anywhere.service..."
if ! timeout 5s systemctl --user stop speak-anywhere.service; then
    echo "Service failed to stop gracefully, force killing..."
    systemctl --user kill -s SIGKILL speak-anywhere.service || true
    systemctl --user stop speak-anywhere.service || true
fi

# Copy binaries
echo "Installing binaries to $INSTALL_DIR..."
install -m 755 "$BUILD_DIR/sa" "$INSTALL_DIR/sa"
install -m 755 "$BUILD_DIR/speak-anywhere" "$INSTALL_DIR/speak-anywhere"

echo "Starting speak-anywhere.service..."
systemctl --user daemon-reload
systemctl --user start speak-anywhere.service

echo "Installation complete!"
