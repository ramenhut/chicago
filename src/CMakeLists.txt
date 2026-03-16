set(CHICAGO_SOURCES
    abac.cpp
    bitstream.cpp
    golomb.cpp
    memory.cpp
    muxer.cpp
    video/common.cpp
    video/convert.cpp
    video/deblock.cpp
    video/decode.cpp
    video/encode.cpp
    video/interp.cpp
    video/intra_pred.cpp
    video/evx3.cpp
    video/evx3dec.cpp
    video/evx3enc.cpp
    video/image.cpp
    video/imageset.cpp
    video/motion.cpp
    video/quantize.cpp
    video/sao.cpp
    video/serialize.cpp
    video/stream.cpp
    video/transform.cpp
    video/unserialize.cpp
    audio/transform.cpp
    audio/types.cpp
    audio/quantize.cpp
    audio/serialize.cpp
    audio/unserialize.cpp
    audio/encode.cpp
    audio/decode.cpp
    audio/evx3.cpp
)

set(CHICAGO_HEADERS
    abac.h
    base.h
    bitstream.h
    golomb.h
    evx_math.h
    evx_memory.h
    muxer.h
    version.h
    video/analysis.h
    video/common.h
    video/config.h
    video/convert.h
    video/interp.h
    video/intra_pred.h
    video/egtables.h
    video/evx3.h
    video/evx3dec.h
    video/evx3enc.h
    video/image.h
    video/imageset.h
    video/macroblock.h
    video/motion.h
    video/quantize.h
    video/sao.h
    video/scan.h
    video/stream.h
    video/transform.h
    video/types.h
    video/xftables.h
    audio/config.h
    audio/transform.h
    audio/types.h
    audio/quantize.h
    audio/serialize.h
    audio/audio.h
)

# Static library
add_library(chicago_codec_static STATIC ${CHICAGO_SOURCES})
target_include_directories(chicago_codec_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/audio>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/video>
)
set_target_properties(chicago_codec_static PROPERTIES OUTPUT_NAME chicago_codec)

# Shared library
add_library(chicago_codec_shared SHARED ${CHICAGO_SOURCES})
target_include_directories(chicago_codec_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/audio>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/video>
)
set_target_properties(chicago_codec_shared PROPERTIES OUTPUT_NAME chicago_codec)

# Threading support for WPP
find_package(Threads REQUIRED)
target_link_libraries(chicago_codec_static Threads::Threads)
target_link_libraries(chicago_codec_shared Threads::Threads)

# Alias for convenience
add_library(chicago::codec ALIAS chicago_codec_static)
