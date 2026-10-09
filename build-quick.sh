#!/bin/bash
# DevilutionX Quick Incremental Build
# Use this for fast iteration during development

set -e

BUILD_DIR="build"

echo "=== Quick Incremental Build ==="
cmake --build "$BUILD_DIR" -j$(nproc)

echo ""
echo "=== Build complete ==="
echo "Executable: $BUILD_DIR/devilutionx"