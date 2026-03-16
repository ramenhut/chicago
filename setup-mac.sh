#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

echo "=== Chicago/EVX3 — macOS Setup ==="

# Install dependencies
if ! command -v brew &>/dev/null; then
    echo "Error: Homebrew not found. Install from https://brew.sh"
    exit 1
fi

echo "Installing dependencies..."
brew install cmake ffmpeg sdl2

# Build
echo "Configuring..."
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo "Building..."
cmake --build build --parallel

# Test
echo "Running tests..."
cd build && ctest --output-on-failure
echo "=== Done ==="
