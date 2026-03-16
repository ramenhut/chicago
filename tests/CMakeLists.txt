# Test suite using Google Test
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# Prevent overriding the parent project's compiler/linker settings (Windows)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Helper function to add a test executable
function(chicago_add_test TEST_NAME)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_NAME}.cpp)
        return()
    endif()
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE chicago::codec GTest::gtest_main)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    gtest_discover_tests(${TEST_NAME})
endfunction()

# Unit tests
chicago_add_test(test_bitstream)
chicago_add_test(test_transform)
chicago_add_test(test_quantize)
chicago_add_test(test_entropy)
chicago_add_test(test_motion)
chicago_add_test(test_convert)
chicago_add_test(test_deblock)
chicago_add_test(test_serialize)

# Integration tests
chicago_add_test(test_codec_roundtrip)
chicago_add_test(test_codec_multiframe)
chicago_add_test(test_codec_quality)
chicago_add_test(test_codec_intra)
chicago_add_test(test_intra_pred)
chicago_add_test(test_interp)

# Audio tests
chicago_add_test(test_audio_transform)
chicago_add_test(test_audio_quantize)
chicago_add_test(test_audio_codec)
chicago_add_test(test_audio_quality)

# Muxer tests
chicago_add_test(test_muxer)
chicago_add_test(test_mux_roundtrip)

# Benchmarks
chicago_add_test(test_benchmark)
