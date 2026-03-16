# Modern CLI tools

# FFmpeg (required for encode/decode/play/analyze)
find_package(FFmpeg)

# SDL2 (required for play tool)
set(CHICAGO_HAS_SDL2 FALSE)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    set(CHICAGO_HAS_SDL2 TRUE)
else()
    # Fall back to PkgConfig
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
        if(SDL2_FOUND)
            set(CHICAGO_HAS_SDL2 TRUE)
        endif()
    endif()
endif()

# VMAF (optional, Linux/macOS only)
set(CHICAGO_HAS_VMAF FALSE)
if(CHICAGO_USE_VMAF)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(VMAF QUIET IMPORTED_TARGET libvmaf)
        if(VMAF_FOUND)
            set(CHICAGO_HAS_VMAF TRUE)
        endif()
    endif()
endif()

if(NOT FFMPEG_FOUND)
    message(STATUS "FFmpeg not found - tools will not be built")
    message(STATUS "  macOS:   brew install ffmpeg")
    message(STATUS "  Linux:   apt install libavformat-dev libavcodec-dev libswscale-dev libavutil-dev")
    message(STATUS "  Windows: vcpkg install ffmpeg:x64-windows")
    return()
endif()

# Shared FFmpeg utility library
add_library(evx_ffmpeg_util STATIC evx_ffmpeg_util.cpp)
target_link_libraries(evx_ffmpeg_util PUBLIC chicago::codec FFmpeg::FFmpeg)
target_include_directories(evx_ffmpeg_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# evx_encode
add_executable(evx_encode evx_encode.cpp)
target_link_libraries(evx_encode PRIVATE evx_ffmpeg_util)

# evx_decode
add_executable(evx_decode evx_decode.cpp)
target_link_libraries(evx_decode PRIVATE evx_ffmpeg_util)

# evx_play (requires SDL2)
if(CHICAGO_HAS_SDL2)
    add_executable(evx_play evx_play.cpp)
    target_link_libraries(evx_play PRIVATE chicago::codec)
    # Link SDL2 via whichever method found it
    if(TARGET SDL2::SDL2)
        target_link_libraries(evx_play PRIVATE SDL2::SDL2)
    elseif(TARGET PkgConfig::SDL2)
        target_link_libraries(evx_play PRIVATE PkgConfig::SDL2)
    endif()
    target_include_directories(evx_play PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
else()
    message(STATUS "SDL2 not found - evx_play will not be built")
    message(STATUS "  macOS:   brew install sdl2")
    message(STATUS "  Linux:   apt install libsdl2-dev")
    message(STATUS "  Windows: vcpkg install sdl2:x64-windows")
endif()

# evx_analyze
add_executable(evx_analyze evx_analyze.cpp)
target_link_libraries(evx_analyze PRIVATE evx_ffmpeg_util)
if(CHICAGO_HAS_VMAF)
    target_compile_definitions(evx_analyze PRIVATE CHICAGO_HAS_VMAF=1)
    target_link_libraries(evx_analyze PRIVATE PkgConfig::VMAF)
endif()

# evx_audio_encode
add_executable(evx_audio_encode evx_audio_encode.cpp)
target_link_libraries(evx_audio_encode PRIVATE evx_ffmpeg_util)

# evx_audio_decode
add_executable(evx_audio_decode evx_audio_decode.cpp)
target_link_libraries(evx_audio_decode PRIVATE evx_ffmpeg_util)

