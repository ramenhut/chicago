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

class QualityTest : public ::testing::Test {
protected:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 64;

    image src_image, dst_image;
    bit_stream coded_stream;

    void SetUp() override {
        create_image(EVX_IMAGE_FORMAT_R8G8B8, WIDTH, HEIGHT, &src_image);
        create_image(EVX_IMAGE_FORMAT_R8G8B8, WIDTH, HEIGHT, &dst_image);
        coded_stream.resize_capacity(4 * EVX_MB * 8);

        // Fill with a moderately complex pattern
        uint8 *data = src_image.query_data();
        for (int j = 0; j < HEIGHT; j++)
        for (int i = 0; i < WIDTH; i++) {
            int offset = j * src_image.query_row_pitch() + i * 3;
            data[offset + 0] = (uint8)((sin(i * 0.2) * 0.5 + 0.5) * 255);
            data[offset + 1] = (uint8)((cos(j * 0.15) * 0.5 + 0.5) * 255);
            data[offset + 2] = (uint8)((sin((i + j) * 0.1) * 0.5 + 0.5) * 255);
        }
    }

    void TearDown() override {
        destroy_image(&src_image);
        destroy_image(&dst_image);
    }

    struct EncodeResult {
        double psnr;
        uint32 size;
    };

    EncodeResult encodeAtQuality(uint8 quality) {
        evx3_encoder *enc = nullptr;
        evx3_decoder *dec = nullptr;
        create_encoder(&enc);
        create_decoder(&dec);

        enc->set_quality(quality);
        coded_stream.empty();

        enc->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream);
        uint32 size = coded_stream.query_byte_occupancy();

        dec->decode(&coded_stream, dst_image.query_data());

        double psnr = compute_psnr(src_image.query_data(), dst_image.query_data(),
                                    WIDTH, HEIGHT, src_image.query_row_pitch());

        destroy_encoder(enc);
        destroy_decoder(dec);

        return {psnr, size};
    }
};

TEST_F(QualityTest, HigherQualityProducesHigherPSNR) {
    auto high = encodeAtQuality(1);    // quality 1 = very high quality
    auto low = encodeAtQuality(28);    // quality 28 = very low quality

    EXPECT_GT(high.psnr, low.psnr)
        << "Quality 1 (PSNR=" << high.psnr << ") should have higher PSNR than quality 28 (PSNR=" << low.psnr << ")";
}

TEST_F(QualityTest, LowerQualityProducesSmallerBitstream) {
    auto high = encodeAtQuality(1);
    auto low = encodeAtQuality(28);

    EXPECT_LT(low.size, high.size)
        << "Quality 28 (" << low.size << " bytes) should produce smaller output than quality 1 (" << high.size << " bytes)";
}

TEST_F(QualityTest, QualityZeroIsHighest) {
    auto q0 = encodeAtQuality(0);
    auto q8 = encodeAtQuality(8);

    EXPECT_GE(q0.psnr, q8.psnr)
        << "Quality 0 should be >= quality 8 in PSNR";
}

TEST_F(QualityTest, MonotonicPSNRTrend) {
    // Test that PSNR generally decreases as quality number increases
    // (quality 0 = best, quality 31 = worst)
    uint8 qualities[] = {0, 4, 8, 16, 28};
    double prev_psnr = 200.0;

    for (int i = 0; i < 5; i++) {
        auto result = encodeAtQuality(qualities[i]);
        // Allow some non-monotonicity due to quantization effects,
        // but the general trend should be downward
        if (i > 0) {
            // Each step shouldn't dramatically increase PSNR
            EXPECT_LT(result.psnr, prev_psnr + 5.0)
                << "Quality " << (int)qualities[i] << " shouldn't be much better than quality " << (int)qualities[i-1];
        }
        prev_psnr = result.psnr;
    }
}

TEST_F(QualityTest, AllQualityLevelsProduceValidOutput) {
    // Ensure all quality levels 0-31 produce decodable output
    for (uint8 q = 0; q < 32; q++) {
        evx3_encoder *enc = nullptr;
        evx3_decoder *dec = nullptr;
        create_encoder(&enc);
        create_decoder(&dec);

        enc->set_quality(q);
        coded_stream.empty();

        EXPECT_EQ(enc->encode(src_image.query_data(), WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS)
            << "Encode failed at quality " << (int)q;
        EXPECT_GT(coded_stream.query_byte_occupancy(), 0u)
            << "Empty output at quality " << (int)q;
        EXPECT_EQ(dec->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS)
            << "Decode failed at quality " << (int)q;

        destroy_encoder(enc);
        destroy_decoder(dec);
    }
}

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)

TEST_F(QualityTest, BFrameMultiFramePSNR) {
    // Encode 8 frames with B-frames and verify all have acceptable PSNR
    evx3_encoder *enc = nullptr;
    evx3_decoder *dec = nullptr;
    create_encoder(&enc);
    create_decoder(&dec);
    enc->set_quality(4);

    const int NUM_FRAMES = 8;
    std::vector<std::vector<uint8>> src_data(NUM_FRAMES);

    bit_stream stream;
    stream.resize_capacity(16 * EVX_MB * 8);

    uint8 *base_data = src_image.query_data();
    uint32 pitch = src_image.query_row_pitch();

    for (int f = 0; f < NUM_FRAMES; f++) {
        // Generate frame with slight motion
        src_data[f].resize(WIDTH * HEIGHT * 3);
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++) {
                int idx = y * WIDTH * 3 + x * 3;
                src_data[f][idx + 0] = (uint8)((sin((x + f * 2) * 0.2) * 0.5 + 0.5) * 255);
                src_data[f][idx + 1] = (uint8)((cos((y + f) * 0.15) * 0.5 + 0.5) * 255);
                src_data[f][idx + 2] = (uint8)((sin((x + y + f * 3) * 0.1) * 0.5 + 0.5) * 255);
            }

        EXPECT_EQ(enc->encode(src_data[f].data(), WIDTH, HEIGHT, &stream), EVX_SUCCESS);
    }

    EXPECT_EQ(enc->flush(&stream), EVX_SUCCESS);

    // Decode and check PSNR
    std::vector<uint8> rgb_buf(WIDTH * HEIGHT * 3);
    std::vector<std::vector<uint8>> decoded;

    int dec_count = 0;
    while (stream.query_byte_occupancy() > 0) {
        if (dec->decode(&stream, rgb_buf.data()) != EVX_SUCCESS) break;
        if (dec->has_output())
            decoded.push_back(std::vector<uint8>(rgb_buf.begin(), rgb_buf.end()));
        dec_count++;
    }
    while (true) {
        dec->flush(rgb_buf.data());
        if (!dec->has_output()) break;
        decoded.push_back(std::vector<uint8>(rgb_buf.begin(), rgb_buf.end()));
    }

    EXPECT_EQ((int)decoded.size(), NUM_FRAMES);

    for (int f = 0; f < (int)decoded.size() && f < NUM_FRAMES; f++) {
        double psnr = compute_psnr(src_data[f].data(), decoded[f].data(),
                                    WIDTH, HEIGHT, WIDTH * 3);
        EXPECT_GT(psnr, 20.0) << "B-frame PSNR too low at frame " << f;
    }

    destroy_encoder(enc);
    destroy_decoder(dec);
}

#endif // EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
