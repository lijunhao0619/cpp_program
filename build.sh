#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
BUILD_TYPE="${1:-Debug}"

if [ ! -d "$BUILD_DIR" ]; then
    echo "==> Configuring CMake ($BUILD_TYPE)..."
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -G "MinGW Makefiles"
fi

echo "==> Building..."
cmake --build "$BUILD_DIR"

echo "==> Done. Binaries in $BUILD_DIR/"
