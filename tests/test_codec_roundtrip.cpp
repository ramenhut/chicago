#include <gtest/gtest.h>
#include <cmath>
#include "video/evx3.h"
#include "video/image.h"
#include "bitstream.h"
#include "video/common.h"
#include "video/config.h"

using namespace evx;

#if EVX_ENABLE_B_FRAMES

TEST(DPBSlotTest, IPFramesCycleSlots01) {
    EXPECT_EQ(evx::assign_ip_dpb_slot(0), 0u);
    EXPECT_EQ(evx::assign_ip_dpb_slot(1), 1u);
    EXPECT_EQ(evx::assign_ip_dpb_slot(2), 0u);
    EXPECT_EQ(evx::assign_ip_dpb_slot(3), 1u);
}

TEST(DPBSlotTest, ReferenceBFrameUsesSlot2) {
    EXPECT_EQ(evx::assign_bref_dpb_slot(), 2u);
}

TEST(DPBSlotTest, NonReferenceBFrameUsesSlot3) {
    EXPECT_EQ(evx::assign_b_scratch_slot(), 3u);
}

TEST(DPBSlotTest, SmallBFrameCountUsesScratchSlot2) {
    EXPECT_EQ(evx::assign_b_scratch_slot_for_count(1), 2u);
    EXPECT_EQ(evx::assign_b_scratch_slot_for_count(2), 2u);
    EXPECT_EQ(evx::assign_b_scratch_slot_for_count(3), 3u);
}

#endif

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

class CodecRoundtripTest : public ::testing::Test {
protected:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 64;

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
    }

    void TearDown() override {
        destroy_encoder(encoder);
        destroy_decoder(decoder);
        destroy_image(&src_image);
        destroy_image(&dst_image);
    }

    void fillGradient() {
        uint8 *data = src_image.query_data();
        for (int j = 0; j < HEIGHT; j++)
        for (int i = 0; i < WIDTH; i++) {
            int offset = j * src_image.query_row_pitch() + i * 3;
            data[offset + 0] = (i * 4) & 0xFF;
            data[offset + 1] = (j * 4) & 0xFF;
            data[offset + 2] = ((i + j) * 2) & 0xFF;
        }
    }

    void fillSolidColor(uint8 r, uint8 g, uint8 b) {
        uint8 *data = src_image.query_data();
        for (int j = 0; j < HEIGHT; j++)
        for (int i = 0; i < WIDTH; i++) {
            int offset = j * src_image.query_row_pitch() + i * 3;
            data[offset + 0] = r;
            data[offset + 1] = g;
            data[offset + 2] = b;
        }
    }
};

TEST_F(CodecRoundtripTest, SingleFrameGradient) {
    fillGradient();
    encoder->set_quality(0); // best quality

    EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS);
    EXPECT_GT(coded_stream.query_byte_occupancy(), 0u) << "Encoded stream should not be empty";

    EXPECT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS);

    double psnr = compute_psnr(src_image.query_data(), dst_image.query_data(),
                                WIDTH, HEIGHT, src_image.query_row_pitch());
    EXPECT_GT(psnr, 25.0) << "PSNR should be reasonable for quality=0";
}

TEST_F(CodecRoundtripTest, SingleFrameSolidColor) {
    fillSolidColor(128, 64, 200);
    encoder->set_quality(0);

    EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS);
    EXPECT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS);

    double psnr = compute_psnr(src_image.query_data(), dst_image.query_data(),
                                WIDTH, HEIGHT, src_image.query_row_pitch());
    EXPECT_GT(psnr, 30.0) << "Solid color should encode very well";
}

TEST_F(CodecRoundtripTest, EncodesDecodesBlack) {
    fillSolidColor(0, 0, 0);
    encoder->set_quality(0);

    EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS);
    EXPECT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS);

    // Black should be very close
    uint8 *dst = dst_image.query_data();
    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        int offset = j * dst_image.query_row_pitch() + i * 3;
        EXPECT_NEAR(dst[offset + 0], 0, 5);
        EXPECT_NEAR(dst[offset + 1], 0, 5);
        EXPECT_NEAR(dst[offset + 2], 0, 5);
    }
}

TEST_F(CodecRoundtripTest, NonZeroBitstreamSize) {
    fillGradient();
    encoder->set_quality(8);

    EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS);

    uint32 size = coded_stream.query_byte_occupancy();
    EXPECT_GT(size, 0u);
    // Encoded size should be less than raw (64*64*3 = 12288 bytes)
    EXPECT_LT(size, (uint32)(WIDTH * HEIGHT * 3));
}

TEST_F(CodecRoundtripTest, EncoderResetWorks) {
    fillGradient();
    encoder->set_quality(4);

    EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS);
    EXPECT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS);

    // Reset and encode again
    encoder->clear();
    decoder->clear();
    coded_stream.empty();

    EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS);
    EXPECT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS);

    double psnr = compute_psnr(src_image.query_data(), dst_image.query_data(),
                                WIDTH, HEIGHT, src_image.query_row_pitch());
    EXPECT_GT(psnr, 20.0);
}

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)

TEST_F(CodecRoundtripTest, BFrameSequenceRoundtrip) {
    // Encode 8 frames (I + B-B-B-P + B-B-B-P pattern) and verify all decode
    encoder->set_quality(4);
    const int NUM_FRAMES = 8;

    // Generate source frames with moving gradient
    std::vector<std::vector<uint8>> src_frames(NUM_FRAMES);
    uint32 pitch = src_image.query_row_pitch();
    uint32 frame_bytes = pitch * HEIGHT;

    // Accumulate all encoded data into one stream
    bit_stream full_stream;
    full_stream.resize_capacity(16 * EVX_MB * 8);

    for (int f = 0; f < NUM_FRAMES; f++) {
        src_frames[f].resize(WIDTH * HEIGHT * 3);
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++) {
                int offset = y * WIDTH * 3 + x * 3;
                src_frames[f][offset + 0] = (uint8)((x + f * 4) % 256);
                src_frames[f][offset + 1] = (uint8)((y + f * 2) % 256);
                src_frames[f][offset + 2] = (uint8)((x + y + f * 3) % 256);
            }

        EXPECT_EQ(encoder->encode(src_frames[f].data(), WIDTH, HEIGHT, &full_stream), EVX_SUCCESS)
            << "Encode failed at frame " << f;
    }

    // Flush remaining
    EXPECT_EQ(encoder->flush(&full_stream), EVX_SUCCESS);

    // Decode all frames
    std::vector<uint8> rgb_buf(WIDTH * HEIGHT * 3);
    int decoded_count = 0;
    std::vector<std::vector<uint8>> decoded_frames;

    // Decode loop: keep calling decode until stream exhausted
    while (full_stream.query_byte_occupancy() > 0) {
        evx_status st = decoder->decode(&full_stream, rgb_buf.data());
        if (st != EVX_SUCCESS) break;

        if (decoder->has_output()) {
            decoded_frames.push_back(std::vector<uint8>(rgb_buf.begin(), rgb_buf.end()));
        }
        decoded_count++;
    }

    // Flush remaining from decoder display buffer
    while (true) {
        decoder->flush(rgb_buf.data());
        if (!decoder->has_output()) break;
        decoded_frames.push_back(std::vector<uint8>(rgb_buf.begin(), rgb_buf.end()));
    }

    // Should have all frames
    EXPECT_EQ((int)decoded_frames.size(), NUM_FRAMES)
        << "Should decode all " << NUM_FRAMES << " frames in display order";

    // Verify PSNR for each frame
    for (int f = 0; f < (int)decoded_frames.size() && f < NUM_FRAMES; f++) {
        double psnr = compute_psnr(src_frames[f].data(), decoded_frames[f].data(),
                                    WIDTH, HEIGHT, WIDTH * 3);
        EXPECT_GT(psnr, 20.0) << "PSNR too low at display frame " << f;
    }
}

#endif // EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
