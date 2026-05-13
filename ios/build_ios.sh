#!/bin/bash
# Build libmahjong-core.a for iOS.
#
# Builds for device (arm64), simulator (arm64), and Mac Catalyst so Xcode
# can link regardless of the selected destination.
#
# Note: Rendering is now done in Swift via Metal. No more Raylib or
# libmahjong-ios.a — only libmahjong-core.a is needed.
#
# Usage:
#   cd ios && ./build_ios.sh
#
# Output:
#   build-ios-device/core_build/libmahjong-core.a
#   build-ios-sim/core_build/libmahjong-core.a
#   build-mac-catalyst/core_build/libmahjong-core.a

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NCPU=$(sysctl -n hw.ncpu)

build_target() {
    local LABEL=$1
    local BUILD_DIR=$2
    local SYSROOT=$3

    echo "=== Building for ${LABEL} (arm64) ==="

    cmake -B "${BUILD_DIR}" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
        -DCMAKE_OSX_SYSROOT="${SYSROOT}" \
        -DCMAKE_BUILD_TYPE=Release \
        "${SCRIPT_DIR}"

    cmake --build "${BUILD_DIR}" --config Release --clean-first -j${NCPU}
    echo "--- ${LABEL} done ---"
    echo ""
}

build_mac_catalyst() {
    local BUILD_DIR="${SCRIPT_DIR}/build-mac-catalyst"
    local MACOS_SDK
    MACOS_SDK=$(xcrun --sdk macosx --show-sdk-path)

    echo "=== Building for Mac Catalyst (arm64) ==="

    cmake -B "${BUILD_DIR}" \
        -DCMAKE_CXX_FLAGS="-target arm64-apple-ios15.0-macabi -isysroot ${MACOS_SDK}" \
        -DCMAKE_C_FLAGS="-target arm64-apple-ios15.0-macabi -isysroot ${MACOS_SDK}" \
        -DCMAKE_BUILD_TYPE=Release \
        "${SCRIPT_DIR}"

    cmake --build "${BUILD_DIR}" --config Release --clean-first -j${NCPU}
    echo "--- Mac Catalyst done ---"
    echo ""
}

build_target "iOS Device" "${SCRIPT_DIR}/build-ios-device" iphoneos
build_target "iOS Simulator" "${SCRIPT_DIR}/build-ios-sim" iphonesimulator
build_mac_catalyst

echo "=== Build complete ==="
for dir in build-ios-device build-ios-sim build-mac-catalyst; do
    echo "${dir} libraries:"
    find "${SCRIPT_DIR}/${dir}" -name "*.a" -maxdepth 2 -type f | while read -r lib; do
        echo "  ${lib}"
    done
done
