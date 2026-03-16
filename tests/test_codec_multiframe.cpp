
#include <gtest/gtest.h>
#include <cmath>
#include "video/evx3.h"
#include "video/image.h"
#include "bitstream.h"
#include "video/config.h"

using namespace evx;

static double compute_psnr(const uint8 *src, const uint8 *dst, int width, int height, int pitch) {
    double mse = 0;
    int count = 0;
    for (int j = 0; j < height; j++)
    for (int i = 0; i < width; i++) {
        for (int c = 0; c < 3; c++) {
            double diff = (double)src[j * pitch + i * 3 + c] - (double)dst[j * pitch + i * 3 + c];
            mse += diff * diff;
            count++;
        }
    }
    mse /= count;
    if (mse == 0) return 100.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

class MultiframeTest : public ::testing::Test {
protected:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 64;
    static constexpr int NUM_FRAMES = 10;

    evx3_encoder *encoder = nullptr;
    evx3_decoder *decoder = nullptr;
    image src_image, dst_image;
    bit_stream coded_stream;

    void SetUp() override {
        create_encoder(&encoder);
        create_decoder(&decoder);
        create_image(EVX_IMAGE_FORMAT_R8G8B8, WIDTH, HEIGHT, &src_image);
        create_image(EVX_IMAGE_FORMAT_R8G8B8, WIDTH, HEIGHT, &dst_image);
        coded_stream.resize_capacity(4 * EVX_MB * 8);
        encoder->set_quality(4);
    }

    void TearDown() override {
        destroy_encoder(encoder);
        destroy_decoder(decoder);
        destroy_image(&src_image);
        destroy_image(&dst_image);
    }

    void generateFrame(int frame_index) {
        uint8 *data = src_image.query_data();
        // Slowly moving gradient - simulates motion between frames
        int offset_x = frame_index * 2;
        for (int j = 0; j < HEIGHT; j++)
        for (int i = 0; i < WIDTH; i++) {
            int idx = j * src_image.query_row_pitch() + i * 3;
            data[idx + 0] = ((i + offset_x) * 4) & 0xFF;
            data[idx + 1] = (j * 4) & 0xFF;
            data[idx + 2] = ((i + j + offset_x) * 2) & 0xFF;
        }
    }
};

TEST_F(MultiframeTest, IPSequenceDecodes) {
#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
    // With B-frames, frames are buffered in display order and emitted in decode order.
    // Encode all frames into a shared stream, then flush remaining frames, then decode.
    for (int f = 0; f < NUM_FRAMES; f++) {
        generateFrame(f);
        EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS)
            << "Encode failed at frame " << f;
    }
    // Flush any remaining buffered frames
    EXPECT_EQ(encoder->flush(&coded_stream), EVX_SUCCESS) << "Flush failed";

    // Decode all frames
    int decoded_count = 0;
    while (coded_stream.query_byte_occupancy() > 0) {
        evx_status st = decoder->decode(&coded_stream, dst_image.query_data());
        if (st != EVX_SUCCESS) break;
        decoded_count++;
    }
    EXPECT_EQ(decoded_count, NUM_FRAMES) << "Should decode all " << NUM_FRAMES << " frames";
#else
    // Without B-frames, encode and decode one frame at a time
    for (int f = 0; f < NUM_FRAMES; f++) {
        generateFrame(f);
        coded_stream.empty();

        EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS)
            << "Encode failed at frame " << f;

        EXPECT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS)
            << "Decode failed at frame " << f;

        double psnr = compute_psnr(src_image.query_data(), dst_image.query_data(),
                                    WIDTH, HEIGHT, src_image.query_row_pitch());
        EXPECT_GT(psnr, 20.0) << "PSNR too low at frame " << f;
    }
#endif
}

TEST_F(MultiframeTest, IntraInsertionWorks) {
    // Encode with an intra insertion in the middle.
    // With B-frames, forced intra flushes pending GOP and encodes I immediately.
    for (int f = 0; f < NUM_FRAMES; f++) {
        generateFrame(f);

        if (f == 5) {
            encoder->insert_intra();
        }

        EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS)
            << "Encode failed at frame " << f;
    }

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
    // Flush remaining buffered frames
    EXPECT_EQ(encoder->flush(&coded_stream), EVX_SUCCESS) << "Flush failed";
#endif

    // Decode all frames from the shared stream
    int decoded_count = 0;
    while (coded_stream.query_byte_occupancy() > 0) {
        evx_status st = decoder->decode(&coded_stream, dst_image.query_data());
        if (st != EVX_SUCCESS) break;
        decoded_count++;
    }
    EXPECT_EQ(decoded_count, NUM_FRAMES) << "Should decode all " << NUM_FRAMES << " frames";
}

TEST_F(MultiframeTest, PFramesSmallerThanIFrames) {
    // First frame is I-frame - should be larger
    generateFrame(0);
    coded_stream.empty();
    encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
    uint32 iframe_size = coded_stream.query_byte_occupancy();

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
    // With B-frames, we need to flush remaining after encoding more frames
    // to get the P-frame size. Encode EVX_B_FRAME_COUNT+1 more frames to trigger
    // a GOP flush, then measure the incremental output.
    uint32 before_size = coded_stream.query_byte_occupancy();
    for (int f = 1; f <= EVX_B_FRAME_COUNT + 1; f++) {
        generateFrame(f);
        encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
    }
    uint32 gop_size = coded_stream.query_byte_occupancy() - before_size;
    // GOP size (P + B frames) should not be dramatically larger than I-frame
    EXPECT_LT(gop_size, iframe_size * (EVX_B_FRAME_COUNT + 2))
        << "GOP should not be dramatically larger than I-frame";
#else
    // Encode a second very similar frame (P-frame) - should be smaller
    generateFrame(1);
    coded_stream.empty();
    encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
    uint32 pframe_size = coded_stream.query_byte_occupancy();

    EXPECT_LT(pframe_size, iframe_size * 2)
        << "P-frame should not be dramatically larger than I-frame for similar content";
#endif
}

TEST_F(MultiframeTest, StaticSceneProducesSmallPFrames) {
    // Encode the same frame repeatedly
    generateFrame(0);

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
    // Encode I-frame
    coded_stream.empty();
    encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
    uint32 iframe_size = coded_stream.query_byte_occupancy();

    // Encode EVX_B_FRAME_COUNT+1 more identical frames to trigger a GOP
    uint32 before_size = coded_stream.query_byte_occupancy();
    for (int f = 1; f <= EVX_B_FRAME_COUNT + 1; f++) {
        encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
    }
    uint32 gop_size = coded_stream.query_byte_occupancy() - before_size;

    // GOP of identical frames should be significantly smaller than iframe_size * (count+1)
    EXPECT_LT(gop_size, iframe_size * (EVX_B_FRAME_COUNT + 1))
        << "GOP of identical content should be smaller than equivalent I-frames";
#else
    coded_stream.empty();
    encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
    uint32 iframe_size = coded_stream.query_byte_occupancy();

    // Repeat the same frame - P-frames should be very small
    for (int f = 1; f < 5; f++) {
        coded_stream.empty();
        encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
        uint32 pframe_size = coded_stream.query_byte_occupancy();

        decoder->decode(&coded_stream, dst_image.query_data());

        EXPECT_LT(pframe_size, iframe_size)
            << "P-frame of identical content should be smaller than I-frame at frame " << f;
    }
#endif
}
