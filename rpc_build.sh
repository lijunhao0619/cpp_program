#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

echo "=== RPC Project Build ==="
echo "Source: $ROOT_DIR"
echo "Build:  $BUILD_DIR"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$ROOT_DIR" \
  -DCMAKE_CXX_COMPILER="E:/msys/ucrt64/bin/g++.exe" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build . -j 4

echo ""
echo "=== Build complete ==="
ls -la rpc_server.exe rpc_client.exe 2>/dev/null
