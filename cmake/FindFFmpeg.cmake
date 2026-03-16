# FindFFmpeg.cmake
# Finds FFmpeg libraries (libavformat, libavcodec, libswscale, libswresample, libavutil)
# Produces an imported target: FFmpeg::FFmpeg
#
# Strategy:
#   1. Try PkgConfig (works on macOS with Homebrew, Linux with system packages)
#   2. Fall back to direct header/library search (works with vcpkg, manual installs)
#
# Set FFMPEG_DIR to hint the search path on Windows or custom installs.

if(TARGET FFmpeg::FFmpeg)
    return()
endif()

set(FFMPEG_FOUND FALSE)

# --- Strategy 1: PkgConfig ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(_FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
    if(_FFMPEG_FOUND)
        add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
        target_link_libraries(FFmpeg::FFmpeg INTERFACE PkgConfig::_FFMPEG)
        set(FFMPEG_FOUND TRUE)
        set(FFMPEG_VERSION "${_FFMPEG_libavformat_VERSION}")
    endif()
endif()

# --- Strategy 2: Direct search (vcpkg, manual install, FFMPEG_DIR) ---
if(NOT FFMPEG_FOUND)
    set(_FFMPEG_SEARCH_PATHS
        ${FFMPEG_DIR}
        $ENV{FFMPEG_DIR}
        /usr/local
        /usr
        /opt/homebrew
    )

    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
        PATHS ${_FFMPEG_SEARCH_PATHS}
        PATH_SUFFIXES include
    )

    set(_FFMPEG_LIBS avformat avcodec swscale swresample avutil)
    set(_FFMPEG_LIB_VARS)

    foreach(_lib ${_FFMPEG_LIBS})
        string(TOUPPER ${_lib} _LIB_UPPER)
        find_library(FFMPEG_${_LIB_UPPER}_LIBRARY
            NAMES ${_lib}
            PATHS ${_FFMPEG_SEARCH_PATHS}
            PATH_SUFFIXES lib lib64
        )
        list(APPEND _FFMPEG_LIB_VARS FFMPEG_${_LIB_UPPER}_LIBRARY)
    endforeach()

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(FFmpeg
        REQUIRED_VARS FFMPEG_INCLUDE_DIR ${_FFMPEG_LIB_VARS}
    )

    if(FFMPEG_FOUND)
        add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
        target_include_directories(FFmpeg::FFmpeg INTERFACE ${FFMPEG_INCLUDE_DIR})
        target_link_libraries(FFmpeg::FFmpeg INTERFACE
            ${FFMPEG_AVFORMAT_LIBRARY}
            ${FFMPEG_AVCODEC_LIBRARY}
            ${FFMPEG_SWSCALE_LIBRARY}
            ${FFMPEG_SWRESAMPLE_LIBRARY}
            ${FFMPEG_AVUTIL_LIBRARY}
        )
    endif()
endif()
