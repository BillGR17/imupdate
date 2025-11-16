#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -euo pipefail

# Define the build directory
BUILD_DIR="./build"

# Configure using CMake
# -S . : Source directory is current directory
# -B ${BUILD_DIR} : Build directory is ./build
# -G "Ninja" : Use the Ninja build system (faster)
# -DCMAKE_BUILD_TYPE=Release : Build an optimized release executable
echo "--- Configuring CMake with Ninja (Release mode) ---"
cmake -S . -B "${BUILD_DIR}" -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# Build the project
echo "--- Building project ---"
cmake --build "${BUILD_DIR}"

echo "--- Build complete! ---"
echo "Executable is at: ${BUILD_DIR}/imupdate"
