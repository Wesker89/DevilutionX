#!/bin/bash
# DevilutionX Linux Build Script
# Usage: ./build.sh [clean|debug|release]

set -e

BUILD_TYPE="${1:-release}"
BUILD_DIR="build"

case "$BUILD_TYPE" in
    clean)
        echo "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        exit 0
        ;;
    debug)
        CMAKE_BUILD_TYPE="Debug"
        ;;
    release|*)
        CMAKE_BUILD_TYPE="Release"
        BUILD_TYPE="release"
        ;;
esac

echo "=== DevilutionX Build ($BUILD_TYPE) ==="
echo "Build directory: $BUILD_DIR"
echo "CMake build type: $CMAKE_BUILD_TYPE"
echo ""

# Configure
cmake -B "$BUILD_DIR" -S . \
    -DBUILD_ASSETS_MPQ=OFF \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
cmake --build "$BUILD_DIR" -j$(nproc)

echo ""
echo "=== Build complete ==="
echo "Executable: $BUILD_DIR/devilutionx"
echo ""
echo "To run (requires X11/Wayland display):"
echo "  cd $BUILD_DIR && ./devilutionx"
echo ""
echo "To run with game assets (copy MPQs first):"
echo "  cp /path/to/DIABDAT.MPQ $BUILD_DIR/"
echo "  cp /path/to/hellfire.mpq $BUILD_DIR/"
echo "  cp /path/to/hf*.mpq $BUILD_DIR/"
echo "  cd $BUILD_DIR && ./devilutionx"