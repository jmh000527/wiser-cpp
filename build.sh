#!/bin/bash

# Build script for Wiser-CPP
# This script provides a simple way to build the project

set -e  # Exit on any error

echo "=== Wiser-CPP Build Script ==="

# Check if cmake is available
if command -v cmake &> /dev/null; then
    echo "Using CMake build system..."
    
    # Create build directory
    mkdir -p build
    cd build
    
    # Configure and build
    cmake ..
    cmake --build .
    
    echo "Build completed!"
    echo "- Main executable is in:   build/bin/ (wiser)"
    echo "- Demo executables are in: ../demo/bin/ (wiser_demo, loader_demo)"

elif command -v make &> /dev/null && command -v g++ &> /dev/null; then
    echo "Using Makefile build system..."
    
    # Build with make
    make all
    
    echo "Build completed!"
    echo "- Main executable is in:   bin/ (wiser)"
    echo "- Demo executables are in: demo/bin/ (wiser_demo, loader_demo)"

else
    echo "Error: Neither CMake nor Make/GCC found!"
    echo "Please install one of the following:"
    echo "  - CMake and a C++ compiler"
    echo "  - Make and GCC/G++"
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
echo "Optional install with CMake (installs wiser, library, headers):"
echo "  cd build"
echo "  cmake --install . --prefix ../install"
echo "Then run installed binary:"
echo "  ../install/bin/wiser -q \"search query\" database.db"
