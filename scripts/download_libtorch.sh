#!/bin/bash
# Download pre-built CPU-only libtorch to third_party/libtorch/
# Usage: ./scripts/download_libtorch.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
INSTALL_DIR="$PROJECT_DIR/third_party"
LIBTORCH_DIR="$INSTALL_DIR/libtorch"

if [ -d "$LIBTORCH_DIR" ] && [ -f "$LIBTORCH_DIR/share/cmake/Torch/TorchConfig.cmake" ]; then
    echo "libtorch already exists at $LIBTORCH_DIR"
    echo "Delete it and re-run this script to re-download."
    exit 0
fi

mkdir -p "$INSTALL_DIR"

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

LIBTORCH_VERSION="2.5.1"

case "$OS" in
    Darwin)
        if [ "$ARCH" = "arm64" ]; then
            URL="https://download.pytorch.org/libtorch/cpu/libtorch-macos-arm64-${LIBTORCH_VERSION}.zip"
        else
            URL="https://download.pytorch.org/libtorch/cpu/libtorch-macos-x86_64-${LIBTORCH_VERSION}.zip"
        fi
        ;;
    Linux)
        URL="https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-${LIBTORCH_VERSION}%2Bcpu.zip"
        ;;
    *)
        echo "Unsupported platform: $OS"
        exit 1
        ;;
esac

echo "Downloading libtorch from: $URL"
echo "This may take a few minutes (~150MB)..."

TEMP_ZIP="$INSTALL_DIR/libtorch.zip"
curl -L -o "$TEMP_ZIP" "$URL"

echo "Extracting..."
cd "$INSTALL_DIR"
unzip -q -o "$TEMP_ZIP"
rm "$TEMP_ZIP"

echo "libtorch installed to: $LIBTORCH_DIR"
echo ""
echo "Build with:"
echo "  cmake -B build -DCMAKE_PREFIX_PATH=$LIBTORCH_DIR && cmake --build build"
