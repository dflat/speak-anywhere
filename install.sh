#!/bin/bash
set -e

# Define paths
BUILD_DIR="build"
INSTALL_DIR="$HOME/.local/bin"
SERVICE_SRC="systemd/speak-anywhere.service"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_DEST="$SERVICE_DIR/speak-anywhere.service"

# Configure build if needed, then build (cmake --build is incremental)
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring cmake build..."
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
fi
echo "Building project..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Create install dir if it doesn't exist
mkdir -p "$INSTALL_DIR"

# Install systemd user service if missing or out of date
mkdir -p "$SERVICE_DIR"
if ! cmp -s "$SERVICE_SRC" "$SERVICE_DEST"; then
    echo "Installing systemd service to $SERVICE_DEST..."
    install -m 644 "$SERVICE_SRC" "$SERVICE_DEST"
    systemctl --user daemon-reload
    systemctl --user enable speak-anywhere.service
fi

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
