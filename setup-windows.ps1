# Chicago/EVX3 — Windows Setup
# Requires: Visual Studio (or Build Tools) with C++ workload, vcpkg

$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot

Write-Host "=== Chicago/EVX3 — Windows Setup ===" -ForegroundColor Cyan

# Check vcpkg
if (-not $env:VCPKG_ROOT) {
    if (Test-Path "$env:USERPROFILE\vcpkg\vcpkg.exe") {
        $env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"
    } else {
        Write-Host "Error: vcpkg not found. Set VCPKG_ROOT or install to %USERPROFILE%\vcpkg" -ForegroundColor Red
        Write-Host "  git clone https://github.com/microsoft/vcpkg && .\vcpkg\bootstrap-vcpkg.bat"
        exit 1
    }
}

$toolchain = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $toolchain)) {
    Write-Host "Error: vcpkg toolchain not found at $toolchain" -ForegroundColor Red
    exit 1
}

# Install dependencies
Write-Host "Installing dependencies..."
& "$env:VCPKG_ROOT\vcpkg.exe" install ffmpeg:x64-windows sdl2:x64-windows

# Build
Write-Host "Configuring..."
cmake -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DVCPKG_TARGET_TRIPLET=x64-windows

Write-Host "Building..."
cmake --build build --config Release --parallel

# Test
Write-Host "Running tests..."
Set-Location build
ctest --build-config Release --output-on-failure

Pop-Location
Write-Host "=== Done ===" -ForegroundColor Cyan
