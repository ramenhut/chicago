cmake_minimum_required(VERSION 3.16)
project(chicago_codec VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Options
option(CHICAGO_BUILD_TOOLS "Build CLI tools (requires FFmpeg, SDL2)" ON)
option(CHICAGO_BUILD_TESTS "Build test suite (requires Google Test)" ON)
option(CHICAGO_USE_VMAF "Enable VMAF metric in analyzer (requires libvmaf)" OFF)

# Additional CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Codec library (always built)
add_subdirectory(src)

# Modern CLI tools
if(CHICAGO_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Tests
if(CHICAGO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
