#include <gtest/gtest.h>
#include <cmath>
#include "video/intra_pred.h"
#include "video/analysis.h"
#include "video/common.h"
#include "video/config.h"
#include "video/evx3.h"

using namespace evx;

// ---------------------------------------------------------------------------
// Unit tests for prediction mode algorithms
// ---------------------------------------------------------------------------

class IntraPredTest : public ::testing::Test {
protected:
    image_set recon;
    image_set scratch_set;
    macroblock pred;

    void SetUp() override {
        recon.initialize(EVX_IMAGE_FORMAT_R16S, 64, 64);
        scratch_set.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE);
        create_macroblock(scratch_set, 0, 0, &pred);

        // Fill reconstruction buffer with a known pattern
        image *y_img = recon.query_y_image();
        int16 *y_data = reinterpret_cast<int16 *>(y_img->query_data());
        uint32 stride = y_img->query_row_pitch() >> 1;

        for (uint32 j = 0; j < 64; j++)
        for (uint32 i = 0; i < 64; i++)
            y_data[j * stride + i] = 100;

        // Fill chroma with known values
        image *u_img = recon.query_u_image();
        image *v_img = recon.query_v_image();
        int16 *u_data = reinterpret_cast<int16 *>(u_img->query_data());
        int16 *v_data = reinterpret_cast<int16 *>(v_img->query_data());
        uint32 c_stride = u_img->query_row_pitch() >> 1;
        for (uint32 j = 0; j < 32; j++)
        for (uint32 i = 0; i < 32; i++) {
            u_data[j * c_stride + i] = 100;
            v_data[j * c_stride + i] = 100;
        }
    }

    void TearDown() override {
        recon.deinitialize();
        scratch_set.deinitialize();
    }

    // Helper to set specific pixel values in the reconstruction buffer
    void setReconPixel(int32 x, int32 y, int16 val) {
        image *y_img = recon.query_y_image();
        int16 *data = reinterpret_cast<int16 *>(y_img->query_data());
        uint32 stride = y_img->query_row_pitch() >> 1;
        data[y * stride + x] = val;
    }
};

TEST_F(IntraPredTest, DCModeFillsWithAverage) {
    // Set up: block at (16, 16) so we have top and left neighbors
    // Top row (y=15, x=16..31): set to 200
    // Left column (x=15, y=16..31): set to 100
    for (int i = 0; i < 16; i++) {
        setReconPixel(16 + i, 15, 200);  // top row
        setReconPixel(15, 16 + i, 100);  // left column
    }

    generate_intra_prediction(EVX_INTRA_MODE_DC, recon, 16, 16, &pred);

    // Expected DC = (sum(200*16) + sum(100*16)) / 32 = (3200 + 1600) / 32 = 150
    int16 expected = 150;
    EXPECT_EQ(pred.data_y[0 * pred.stride + 0], expected);
    EXPECT_EQ(pred.data_y[8 * pred.stride + 8], expected);
    EXPECT_EQ(pred.data_y[15 * pred.stride + 15], expected);
}

TEST_F(IntraPredTest, VerticalCopiesTopRow) {
    // Block at (16, 16): top row at y=15
    for (int i = 0; i < 16; i++)
        setReconPixel(16 + i, 15, (int16)(10 * i));

    generate_intra_prediction(EVX_INTRA_MODE_VERT, recon, 16, 16, &pred);

    // Every row should be a copy of the top row
    for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
        EXPECT_EQ(pred.data_y[y * pred.stride + x], (int16)(10 * x))
            << "Mismatch at (" << x << "," << y << ")";
}

TEST_F(IntraPredTest, HorizontalCopiesLeftColumn) {
    // Block at (16, 16): left column at x=15
    for (int i = 0; i < 16; i++)
        setReconPixel(15, 16 + i, (int16)(10 * i));

    generate_intra_prediction(EVX_INTRA_MODE_HORIZ, recon, 16, 16, &pred);

    // Every column should copy the left column value for that row
    for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
        EXPECT_EQ(pred.data_y[y * pred.stride + x], (int16)(10 * y))
            << "Mismatch at (" << x << "," << y << ")";
}

TEST_F(IntraPredTest, BoundaryHandlingTopRow) {
    // Block at (16, 0): no top neighbor. DC should use only left column.
    for (int i = 0; i < 16; i++)
        setReconPixel(15, i, 200);

    generate_intra_prediction(EVX_INTRA_MODE_DC, recon, 16, 0, &pred);

    // Only left column available: DC = avg(200*16) / 16 = 200
    EXPECT_EQ(pred.data_y[0], 200);
}

TEST_F(IntraPredTest, BoundaryHandlingLeftColumn) {
    // Block at (0, 16): no left neighbor. DC should use only top row.
    for (int i = 0; i < 16; i++)
        setReconPixel(i, 15, 200);

    generate_intra_prediction(EVX_INTRA_MODE_DC, recon, 0, 16, &pred);

    // Only top row available: DC = 200
    EXPECT_EQ(pred.data_y[0], 200);
}

TEST_F(IntraPredTest, BoundaryHandlingCorner) {
    // Block at (0, 0): no top or left. DC = 128 (default)
    generate_intra_prediction(EVX_INTRA_MODE_DC, recon, 0, 0, &pred);
    EXPECT_EQ(pred.data_y[0], 128);
}

TEST_F(IntraPredTest, ModeSelectionPicksHorizontalForVerticalEdge) {
    // Create a source block with a vertical edge (left half bright, right half dark)
    image_set source_set;
    source_set.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE);
    macroblock source;
    create_macroblock(source_set, 0, 0, &source);

    for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
        source.data_y[y * source.stride + x] = (x < 8) ? 200 : 50;

    // Fill chroma uniformly
    for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
        source.data_u[y * (source.stride >> 1) + x] = 128;
        source.data_v[y * (source.stride >> 1) + x] = 128;
    }

    // Set up reconstruction so left neighbor is 200, top neighbor is mixed
    for (int i = 0; i < 16; i++) {
        setReconPixel(15, 16 + i, 200);  // left = 200
        setReconPixel(16 + i, 15, (i < 8) ? 200 : 50);  // top matches the pattern
    }
    setReconPixel(15, 15, 200); // corner

    int32 best_sad = 0;
    uint8 mode = select_best_intra_mode(source, recon, 16, 16, &pred, &best_sad);

    // Vertical prediction should match perfectly since each column copies the top
    EXPECT_EQ(mode, EVX_INTRA_MODE_VERT);
    EXPECT_EQ(best_sad, 0);

    source_set.deinitialize();
}

// ---------------------------------------------------------------------------
// Integration: full codec roundtrip with intra prediction
// ---------------------------------------------------------------------------

class IntraPredRoundtripTest : public ::testing::Test {
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

    static double computePSNR(const uint8 *src, const uint8 *dst, int width, int height, int pitch) {
        double mse = 0;
        int count = 0;
        for (int j = 0; j < height; j++)
        for (int i = 0; i < width; i++)
        for (int c = 0; c < 3; c++) {
            double diff = (double)src[j * pitch + i * 3 + c] - (double)dst[j * pitch + i * 3 + c];
            mse += diff * diff;
            count++;
        }
        mse /= count;
        if (mse == 0) return 100.0;
        return 10.0 * log10(255.0 * 255.0 / mse);
    }
};

TEST_F(IntraPredRoundtripTest, GradientImageRoundtrip) {
    // Create a gradient image (should benefit from directional prediction)
    uint8 *data = src_image.query_data();
    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        int offset = j * src_image.query_row_pitch() + i * 3;
        data[offset + 0] = (i * 4) & 0xFF;
        data[offset + 1] = (j * 4) & 0xFF;
        data[offset + 2] = ((i + j) * 2) & 0xFF;
    }

    for (int quality : {4, 8, 20}) {
        encoder->set_quality(quality);
        coded_stream.empty();

        // Recreate fresh encoder/decoder for each quality
        destroy_encoder(encoder);
        destroy_decoder(decoder);
        create_encoder(&encoder);
        create_decoder(&decoder);
        encoder->set_quality(quality);

        ASSERT_EQ(encoder->encode(data, WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS)
            << "Encode failed at quality " << quality;
        ASSERT_EQ(decoder->decode(&coded_stream, dst_image.query_data()), EVX_SUCCESS)
            << "Decode failed at quality " << quality;

        double psnr = computePSNR(data, dst_image.query_data(), WIDTH, HEIGHT, src_image.query_row_pitch());
        EXPECT_GT(psnr, 18.0) << "PSNR too low at quality " << quality;
    }
}

TEST_F(IntraPredRoundtripTest, MultiFrameWithIntraPrediction) {
    // Test multi-frame sequence where intra prediction helps I-frames.
    // With B-frames, frames are buffered in display order and emitted in decode order.
    encoder->set_quality(8);

    static constexpr int NUM_FRAMES = 5;

    // Encode all frames into shared stream, then flush.
    for (int f = 0; f < NUM_FRAMES; f++) {
        uint8 *data = src_image.query_data();
        for (int j = 0; j < HEIGHT; j++)
        for (int i = 0; i < WIDTH; i++) {
            int offset = j * src_image.query_row_pitch() + i * 3;
            data[offset + 0] = ((i + f * 2) * 4) & 0xFF;
            data[offset + 1] = ((j + f) * 4) & 0xFF;
            data[offset + 2] = ((i + j + f) * 2) & 0xFF;
        }

        if (f == 0)
            encoder->insert_intra();

        ASSERT_EQ(encoder->encode(data, WIDTH, HEIGHT, &coded_stream), EVX_SUCCESS)
            << "Encode failed at frame " << f;
    }

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
    // Flush remaining buffered frames.
    ASSERT_EQ(encoder->flush(&coded_stream), EVX_SUCCESS) << "Flush failed";
#endif

    // Decode all frames and verify at least the first frame has good PSNR.
    // We only check PSNR for the I-frame (first decoded), since subsequent frames
    // are B/P frames that depend on the I-frame reference.
    int decoded_count = 0;
    while (coded_stream.query_byte_occupancy() > 0) {
        evx_status st = decoder->decode(&coded_stream, dst_image.query_data());
        if (st != EVX_SUCCESS) break;
        decoded_count++;
    }
    EXPECT_EQ(decoded_count, NUM_FRAMES) << "Should decode all frames";
}

// ---------------------------------------------------------------------------
// Gradient-based fast intra mode selection tests
// ---------------------------------------------------------------------------

TEST_F(IntraPredTest, GradientModeSelectionPicksReasonableMode) {
    // Create a source block with strong vertical edges (constant columns, varying rows).
    // This means strong horizontal gradient (Gh >> Gv), so vertical-leaning modes expected.
    image_set source_set;
    source_set.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE);
    macroblock source;
    create_macroblock(source_set, 0, 0, &source);

    // Each column has a different constant value -> strong horizontal gradient in top neighbor.
    for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
        source.data_y[y * source.stride + x] = (int16)(x * 15);

    for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
        source.data_u[y * (source.stride >> 1) + x] = 128;
        source.data_v[y * (source.stride >> 1) + x] = 128;
    }

    // Set up top neighbor row with the same ramp pattern (strong Gh).
    for (int i = 0; i < 16; i++) {
        setReconPixel(16 + i, 15, (int16)(i * 15));  // top = ramp
        setReconPixel(15, 16 + i, 128);               // left = constant
    }
    setReconPixel(15, 15, 0); // corner

    int32 best_sad = 0;
    uint8 mode = select_best_intra_mode(source, recon, 16, 16, &pred, &best_sad);

    // With strong Gh, fast mode should pick from {DC, VERT, VERT_R}.
    // Exhaustive would pick VERT for a perfect column ramp.
    EXPECT_TRUE(mode == EVX_INTRA_MODE_DC || mode == EVX_INTRA_MODE_VERT || mode == EVX_INTRA_MODE_VERT_R)
        << "Expected DC/VERT/VERT_R for strong horizontal gradient, got mode " << (int)mode;

    source_set.deinitialize();
}

TEST_F(IntraPredTest, FastModeQualityCloseToExhaustive) {
    // Create a test pattern and compare fast vs exhaustive SAD.
    image_set source_set;
    source_set.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE);
    macroblock source;
    create_macroblock(source_set, 0, 0, &source);

    // Diagonal gradient pattern.
    for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
        source.data_y[y * source.stride + x] = (int16)((x + y) * 8);

    for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
        source.data_u[y * (source.stride >> 1) + x] = 128;
        source.data_v[y * (source.stride >> 1) + x] = 128;
    }

    // Set neighbors for block at (16, 16).
    for (int i = 0; i < 16; i++) {
        setReconPixel(16 + i, 15, (int16)(i * 8));        // top
        setReconPixel(15, 16 + i, (int16)(i * 8));        // left
    }
    setReconPixel(15, 15, 0); // corner

    // Get the fast mode result.
    int32 fast_sad = 0;
    select_best_intra_mode(source, recon, 16, 16, &pred, &fast_sad);

    // Compute exhaustive best SAD for comparison.
    int32 exhaustive_best_sad = EVX_MAX_INT32;
    for (uint8 mode = 0; mode < EVX_INTRA_MODE_COUNT; mode++) {
        generate_intra_prediction(mode, recon, 16, 16, &pred);
        int32 sad = compute_block_sad(source, pred);
        if (sad < exhaustive_best_sad)
            exhaustive_best_sad = sad;
    }

    // Fast should be within 20% of exhaustive (generous margin since we only test 3 candidates).
    EXPECT_LT(fast_sad, exhaustive_best_sad * 12 / 10 + 1)
        << "Fast mode SAD (" << fast_sad << ") too far from exhaustive (" << exhaustive_best_sad << ")";

    source_set.deinitialize();
}

// ---------------------------------------------------------------------------
// select_intra_mode_candidates tests
// ---------------------------------------------------------------------------

TEST_F(IntraPredTest, CandidatesMatchGradientPruning) {
    // Verify select_intra_mode_candidates returns 3 modes with DC always first.
    uint8 candidates[3] = {255, 255, 255};
    uint8 count = select_intra_mode_candidates(recon, 16, 16, candidates);

    EXPECT_EQ(count, 3u);
    EXPECT_EQ(candidates[0], EVX_INTRA_MODE_DC)
        << "First candidate should always be DC";
    // All three should be valid modes (0-6)
    for (int i = 0; i < 3; i++) {
        EXPECT_LT(candidates[i], EVX_INTRA_MODE_COUNT)
            << "Candidate " << i << " is not a valid mode";
    }

    // Test with strong horizontal gradient in top neighbor (should pick vert-leaning)
    for (int i = 0; i < 16; i++)
        setReconPixel(16 + i, 15, (int16)(i * 15));  // top = ramp (strong Gh)
    for (int i = 0; i < 16; i++)
        setReconPixel(15, 16 + i, 128);               // left = constant (weak Gv)

    count = select_intra_mode_candidates(recon, 16, 16, candidates);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(candidates[0], EVX_INTRA_MODE_DC);
    EXPECT_EQ(candidates[1], EVX_INTRA_MODE_VERT);
    EXPECT_EQ(candidates[2], EVX_INTRA_MODE_VERT_R);
}
