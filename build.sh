#!/bin/bash

# Build script for Wiser-CPP
# This script provides a simple way to build the project

set -e  # Exit on any error

echo "=== Wiser-CPP Build Script ==="

# Determine the build directory and type
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${1:-${BUILD_TYPE:-Release}}"

echo "Build directory : ${BUILD_DIR}"
echo "Build type      : ${BUILD_TYPE}"

# Check if cmake is available
if command -v cmake &> /dev/null; then
    echo "Using CMake build system..."
    
    # Create build directory
    mkdir -p "${BUILD_DIR}"

    # Configure and build
    cmake -S "$(dirname "$0")" -B "${BUILD_DIR}" \
          -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
          -DCMAKE_CONFIGURATION_TYPES="${BUILD_TYPE}" || true

    cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j

    echo ""
    echo "Build completed!"
    echo "- Executables are in: $(dirname "$0")/bin/ (wiser, wiser_web)"
    echo "- Demo executables are in: $(dirname "$0")/demo/bin/ (wiser_demo, loader_demo)"

else
    echo "Error: CMake not found! Please install CMake and a C++ compiler."
    exit 1
fi

cd "$(dirname "$0")"

echo ""
echo "To run the demos:"
echo "  ./demo/bin/wiser_demo"
echo "  ./demo/bin/loader_demo"
echo ""
echo "To run the main program:"
echo "  ./bin/wiser -q \"search query\" database.db"
echo ""
echo "To run the web server:"
echo "  ./bin/wiser_web my.db --phrase=off"
echo ""
echo "Optional install with CMake (installs wiser, library, headers):"
echo "  cd build"
echo "  cmake --install . --config Release --prefix ../install"
echo ""
echo "Then run installed binaries:"
echo "  ../install/bin/wiser -q \"search query\" database.db"
echo "  ../install/bin/wiser_web ../install/data/my.db"
