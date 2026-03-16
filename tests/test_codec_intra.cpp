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

class IntraOnlyTest : public ::testing::Test {
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
        encoder->set_quality(4);
    }

    void TearDown() override {
        destroy_encoder(encoder);
        destroy_decoder(decoder);
        destroy_image(&src_image);
        destroy_image(&dst_image);
    }

    void fillPattern(int frame_idx) {
        uint8 *data = src_image.query_data();
        int shift = frame_idx * 3;
        for (int j = 0; j < HEIGHT; j++)
        for (int i = 0; i < WIDTH; i++) {
            int offset = j * src_image.query_row_pitch() + i * 3;
            data[offset + 0] = ((i + shift) * 4) & 0xFF;
            data[offset + 1] = ((j + shift) * 4) & 0xFF;
            data[offset + 2] = ((i + j + shift) * 2) & 0xFF;
        }
    }
};

TEST_F(IntraOnlyTest, ForceIntraEveryFrame) {
    // Force I-frame on every encode by calling insert_intra before each
    for (int f = 0; f < 5; f++) {
        fillPattern(f);
        coded_stream.empty();

        encoder->insert_intra();
        EXPECT_EQ(encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS)
            << "Encode failed at frame " << f;
        EXPECT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS)
            << "Decode failed at frame " << f;

        double psnr = compute_psnr(src_image.query_data(), dst_image.query_data(),
                                    WIDTH, HEIGHT, src_image.query_row_pitch());
        EXPECT_GT(psnr, 20.0) << "PSNR too low at intra frame " << f;
    }
}

TEST_F(IntraOnlyTest, IntraFrameDecodesIndependently) {
    // Verify that an I-frame can be decoded by a fresh encoder/decoder pair
    // without needing any prior state. The codec's adaptive entropy state means
    // encoder and decoder must be paired, but each I-frame should not depend
    // on having decoded previous frames.

    // Encode one frame to advance the encoder state
    fillPattern(0);
    coded_stream.empty();
    encoder->insert_intra();
    encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
    // Don't decode this frame - skip it

    // Create completely fresh encoder and decoder pair
    destroy_encoder(encoder);
    destroy_decoder(decoder);
    create_encoder(&encoder);
    create_decoder(&decoder);
    encoder->set_quality(4);

    // Encode and decode a fresh I-frame
    fillPattern(1);
    coded_stream.empty();
    encoder->insert_intra();
    encoder->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
    EXPECT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS);

    double psnr = compute_psnr(src_image.query_data(), dst_image.query_data(),
                                WIDTH, HEIGHT, src_image.query_row_pitch());
    EXPECT_GT(psnr, 20.0) << "I-frame should decode independently";
}

TEST_F(IntraOnlyTest, AllIntraQualityConsistency) {
    // All intra frames should have similar PSNR for same content at same quality
    double psnr_values[5];

    for (int f = 0; f < 5; f++) {
        fillPattern(0); // Same content each time
        coded_stream.empty();

        // Create fresh encoder/decoder each time to ensure true independence
        evx3_encoder *enc = nullptr;
        evx3_decoder *dec = nullptr;
        create_encoder(&enc);
        create_decoder(&dec);
        enc->set_quality(4);

        enc->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
        dec->decode(&coded_stream, dst_image.query_data());

        psnr_values[f] = compute_psnr(src_image.query_data(), dst_image.query_data(),
                                       WIDTH, HEIGHT, src_image.query_row_pitch());

        destroy_encoder(enc);
        destroy_decoder(dec);
    }

    // All PSNR values should be identical (same input, same encoder state)
    for (int f = 1; f < 5; f++) {
        EXPECT_NEAR(psnr_values[f], psnr_values[0], 0.01)
            << "PSNR should be consistent for same input at frame " << f;
    }
}
