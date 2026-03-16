#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

echo "=== Chicago/EVX3 — Linux Setup ==="

# Detect package manager and install dependencies
if command -v apt-get &>/dev/null; then
    echo "Installing dependencies (apt)..."
    sudo apt-get update
    sudo apt-get install -y cmake g++ \
        libavformat-dev libavcodec-dev libswscale-dev libavutil-dev \
        libsdl2-dev
elif command -v dnf &>/dev/null; then
    echo "Installing dependencies (dnf)..."
    sudo dnf install -y cmake gcc-c++ \
        ffmpeg-devel SDL2-devel
else
    echo "Error: No supported package manager found (apt or dnf)"
    exit 1
fi

# Build
echo "Configuring..."
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo "Building..."
cmake --build build --parallel

# Test
echo "Running tests..."
cd build && ctest --output-on-failure
echo "=== Done ==="
