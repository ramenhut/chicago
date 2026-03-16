#include <gtest/gtest.h>
#include "video/analysis.h"
#include "video/macroblock.h"
#include "video/motion.h"
#include "video/image.h"
#include "video/imageset.h"

using namespace evx;

// The macroblock struct holds pointers (data_y, data_u, data_v) that alias
// into an external buffer -- it does NOT embed arrays.  Tests must allocate
// backing memory and point the macroblock into it.

static const uint32 MB_STRIDE     = EVX_MACROBLOCK_SIZE;                          // 16 elements
static const uint32 LUMA_COUNT    = MB_STRIDE * EVX_MACROBLOCK_SIZE;              // 16*16
static const uint32 CHROMA_STRIDE = MB_STRIDE >> 1;                               // 8
static const uint32 CHROMA_COUNT  = CHROMA_STRIDE * (EVX_MACROBLOCK_SIZE >> 1);   // 8*8

// Helper: allocate backing storage and wire a macroblock to it.
static void alloc_macroblock(macroblock &mb, int16 *&buf_y, int16 *&buf_u, int16 *&buf_v) {
    buf_y = new int16[LUMA_COUNT]();
    buf_u = new int16[CHROMA_COUNT]();
    buf_v = new int16[CHROMA_COUNT]();
    mb.data_y  = buf_y;
    mb.data_u  = buf_u;
    mb.data_v  = buf_v;
    mb.stride  = MB_STRIDE;
}

static void free_macroblock_bufs(int16 *buf_y, int16 *buf_u, int16 *buf_v) {
    delete[] buf_y;
    delete[] buf_u;
    delete[] buf_v;
}

class MotionMetricTest : public ::testing::Test {
protected:
    macroblock a, b;
    int16 *a_y, *a_u, *a_v;
    int16 *b_y, *b_u, *b_v;

    void SetUp() override {
        alloc_macroblock(a, a_y, a_u, a_v);
        alloc_macroblock(b, b_y, b_u, b_v);
    }

    void TearDown() override {
        free_macroblock_bufs(a_y, a_u, a_v);
        free_macroblock_bufs(b_y, b_u, b_v);
    }
};

TEST_F(MotionMetricTest, SADIdenticalBlocksIsZero) {
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = (i + j * 3) % 200;
        b.data_y[j * b.stride + i] = (i + j * 3) % 200;
    }
    EXPECT_EQ(compute_block_sad(a, b), 0);
}

TEST_F(MotionMetricTest, SADKnownDifference) {
    // All a = 100, all b = 110 -> SAD = 16*16*10 = 2560
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 100;
        b.data_y[j * b.stride + i] = 110;
    }
    EXPECT_EQ(compute_block_sad(a, b), 16 * 16 * 10);
}

TEST_F(MotionMetricTest, SADSingleBlock) {
    // SAD of a single block = sum of abs values
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 5;
    }
    EXPECT_EQ(compute_block_sad(a), 16 * 16 * 5);
}

TEST_F(MotionMetricTest, MADIdenticalBlocksIsZero) {
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        int16 val = (i + j) % 128;
        a.data_y[j * a.stride + i] = val;
        b.data_y[j * b.stride + i] = val;
    }
    for (int j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); j++)
    for (int i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); i++) {
        a.data_u[j * (a.stride >> 1) + i] = 50;
        b.data_u[j * (b.stride >> 1) + i] = 50;
        a.data_v[j * (a.stride >> 1) + i] = 60;
        b.data_v[j * (b.stride >> 1) + i] = 60;
    }
    EXPECT_EQ(compute_block_mad(a, b), 0);
}

TEST_F(MotionMetricTest, MADFindsMaxDifference) {
    // All same except one pixel differs by 42
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 100;
        b.data_y[j * b.stride + i] = 100;
    }
    for (int j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); j++)
    for (int i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); i++) {
        a.data_u[j * (a.stride >> 1) + i] = 0;
        b.data_u[j * (b.stride >> 1) + i] = 0;
        a.data_v[j * (a.stride >> 1) + i] = 0;
        b.data_v[j * (b.stride >> 1) + i] = 0;
    }
    b.data_y[5 * b.stride + 7] = 142;
    EXPECT_EQ(compute_block_mad(a, b), 42);
}

TEST_F(MotionMetricTest, SSDIdenticalBlocksIsZero) {
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 77;
        b.data_y[j * b.stride + i] = 77;
    }
    EXPECT_EQ(compute_block_ssd(a, b), 0);
}

TEST_F(MotionMetricTest, SSDKnownDifference) {
    // All a = 100, all b = 103 -> SSD = 16*16*9 = 2304
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 100;
        b.data_y[j * b.stride + i] = 103;
    }
    EXPECT_EQ(compute_block_ssd(a, b), 16 * 16 * 9);
}

TEST_F(MotionMetricTest, MSEKnownValue) {
    // All a = 0, all b = 4 -> MSE = (16*16*16) / 256 = 16
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 0;
        b.data_y[j * b.stride + i] = 4;
    }
    EXPECT_EQ(compute_block_mse(a, b), 16);
}

TEST_F(MotionMetricTest, BlockMeanKnownValue) {
    // All pixels = 128 -> mean should be 128
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 128;
    }
    EXPECT_EQ(compute_block_mean(a), 128);
}

TEST_F(MotionMetricTest, VarianceOfConstantIsZero) {
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 50;
    }
    EXPECT_EQ(compute_block_variance(a), 0);
}

TEST_F(MotionMetricTest, SubpixelInterpolation) {
    // Test lerp_macroblock_half: result should be midpoint
    macroblock c, result;
    int16 *c_y, *c_u, *c_v;
    int16 *r_y, *r_u, *r_v;
    alloc_macroblock(c, c_y, c_u, c_v);
    alloc_macroblock(result, r_y, r_u, r_v);

    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 100;
        c.data_y[j * c.stride + i] = 200;
    }
    for (int j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); j++)
    for (int i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); i++) {
        a.data_u[j * (a.stride >> 1) + i] = 100;
        c.data_u[j * (c.stride >> 1) + i] = 200;
        a.data_v[j * (a.stride >> 1) + i] = 100;
        c.data_v[j * (c.stride >> 1) + i] = 200;
    }

    lerp_macroblock_half(a, c, &result);

    // Result should be approximately 150 (midpoint of 100 and 200)
    EXPECT_NEAR(result.data_y[0], 150, 2);
    EXPECT_NEAR(result.data_y[8 * result.stride + 8], 150, 2);

    free_macroblock_bufs(c_y, c_u, c_v);
    free_macroblock_bufs(r_y, r_u, r_v);
}

// --- MV Predictor Tests ---

class MVPredictorTest : public ::testing::Test {
protected:
    static constexpr uint32 W = 4; // 4 blocks wide
    static constexpr uint32 H = 4; // 4 blocks tall
    static constexpr uint32 N = W * H;
    evx_block_desc blocks[W * H];

    void SetUp() override {
        for (uint32 i = 0; i < N; i++) {
            clear_block_desc(&blocks[i]);
        }
    }
};

TEST_F(MVPredictorTest, NoMotionNeighborsReturnsZero) {
    // All blocks are INTRA_DEFAULT (no motion), predictor for any block should be (0,0)
    blocks[5].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[5].motion_x = 10;
    blocks[5].motion_y = -5;

    int16 px, py;
    // Block 0 has no left/top neighbors at all
    compute_mv_predictor(blocks, W, 0,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    EXPECT_EQ(px, 0);
    EXPECT_EQ(py, 0);

    // Block 1 has left neighbor (block 0) which is INTRA_DEFAULT (non-motion)
    compute_mv_predictor(blocks, W, 1,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    EXPECT_EQ(px, 0);
    EXPECT_EQ(py, 0);
}

TEST_F(MVPredictorTest, SingleLeftNeighbor) {
    // Block at (1,0) = index 1 has left neighbor at (0,0) = index 0
    blocks[0].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[0].motion_x = 4;
    blocks[0].motion_y = -3;

    blocks[1].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[1].motion_x = 10;
    blocks[1].motion_y = 5;

    int16 px, py;
    compute_mv_predictor(blocks, W, 1,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    EXPECT_EQ(px, 4);
    EXPECT_EQ(py, -3);
}

TEST_F(MVPredictorTest, SingleTopNeighbor) {
    // Block at (0,1) = index 4 has top neighbor at (0,0) = index 0
    blocks[0].block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    blocks[0].motion_x = -2;
    blocks[0].motion_y = 7;

    blocks[4].block_type = EVX_BLOCK_INTER_MOTION_DELTA;

    int16 px, py;
    compute_mv_predictor(blocks, W, 4,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    EXPECT_EQ(px, -2);
    EXPECT_EQ(py, 7);
}

TEST_F(MVPredictorTest, MedianOfThreeNeighbors) {
    // Block at (1,1) = index 5 has:
    //   left  (0,1) = index 4: mv=(2, 6)
    //   top   (1,0) = index 1: mv=(8, 1)
    //   top-r (2,0) = index 2: mv=(5, 3)
    // Median x: median(2,8,5) = 5
    // Median y: median(6,1,3) = 3

    blocks[4].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[4].motion_x = 2; blocks[4].motion_y = 6;

    blocks[1].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[1].motion_x = 8; blocks[1].motion_y = 1;

    blocks[2].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[2].motion_x = 5; blocks[2].motion_y = 3;

    blocks[5].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;

    int16 px, py;
    compute_mv_predictor(blocks, W, 5,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    EXPECT_EQ(px, 5);
    EXPECT_EQ(py, 3);
}

TEST_F(MVPredictorTest, TwoNeighborsAverage) {
    // Block at (1,1) = index 5:
    //   left  (0,1) = index 4: mv=(3, 4) — motion
    //   top   (1,0) = index 1: mv=(7, 2) — motion
    //   top-r (2,0) = index 2: non-motion
    //   top-l (0,0) = index 0: non-motion
    // Only 2 neighbors → average: ((3+7)/2, (4+2)/2) = (5, 3)

    blocks[4].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[4].motion_x = 3; blocks[4].motion_y = 4;

    blocks[1].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[1].motion_x = 7; blocks[1].motion_y = 2;

    blocks[2].block_type = EVX_BLOCK_INTRA_DEFAULT; // non-motion
    blocks[0].block_type = EVX_BLOCK_INTRA_DEFAULT; // non-motion (top-left fallback also non-motion)

    blocks[5].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;

    int16 px, py;
    compute_mv_predictor(blocks, W, 5,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    EXPECT_EQ(px, 5);
    EXPECT_EQ(py, 3);
}

TEST_F(MVPredictorTest, TopRightFallsBackToTopLeft) {
    // Block at (3,1) = index 7. Top-right (4,0) is out of bounds (W=4).
    // Should fall back to top-left (2,0) = index 2.
    blocks[6].block_type = EVX_BLOCK_INTRA_MOTION_DELTA; // left (2,1)
    blocks[6].motion_x = 1; blocks[6].motion_y = 1;

    blocks[3].block_type = EVX_BLOCK_INTRA_MOTION_DELTA; // top (3,0)
    blocks[3].motion_x = 5; blocks[3].motion_y = 5;

    blocks[2].block_type = EVX_BLOCK_INTRA_MOTION_DELTA; // top-left (2,0) — fallback
    blocks[2].motion_x = 3; blocks[2].motion_y = 3;

    blocks[7].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;

    int16 px, py;
    compute_mv_predictor(blocks, W, 7,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    // median(1,5,3) = 3 for both x and y
    EXPECT_EQ(px, 3);
    EXPECT_EQ(py, 3);
}

TEST_F(MVPredictorTest, FirstRowLeftOnly) {
    // Block at (2,0) = index 2. No top row. Left neighbor index 1.
    blocks[1].block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    blocks[1].motion_x = -4; blocks[1].motion_y = 6;

    blocks[2].block_type = EVX_BLOCK_INTER_MOTION_DELTA;

    int16 px, py;
    compute_mv_predictor(blocks, W, 2,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    EXPECT_EQ(px, -4);
    EXPECT_EQ(py, 6);
}

TEST_F(MVPredictorTest, FirstColumnTopOnly) {
    // Block at (0,2) = index 8. No left neighbor. Top neighbor index 4.
    blocks[4].block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    blocks[4].motion_x = 10; blocks[4].motion_y = -8;

    // Top-right: (1,1) = index 5
    blocks[5].block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    blocks[5].motion_x = 6; blocks[5].motion_y = -2;

    blocks[8].block_type = EVX_BLOCK_INTER_MOTION_DELTA;

    int16 px, py;
    compute_mv_predictor(blocks, W, 8,
#if EVX_ENABLE_TEMPORAL_MVP
                      nullptr,
#endif
                      &px, &py);
    // 2 neighbors → average: ((10+6)/2, (-8+-2)/2) = (8, -5)
    EXPECT_EQ(px, 8);
    EXPECT_EQ(py, -5);
}

// --- SATD / Hadamard Tests ---

TEST_F(MotionMetricTest, HadamardKnownInput) {
    // 4x4 block: all 1s → Hadamard should concentrate energy at DC
    int16 input[16];
    int32 output[16];
    for (int i = 0; i < 16; i++) input[i] = 1;

    hadamard_4x4(input, 4, output);

    // DC coefficient should be 16 (sum of all 1s × 4 rows × horizontal)
    // Actually: horizontal pass on all-1 row: [4, 0, 0, 0]. Vertical pass on column 0: [16, 0, 0, 0].
    EXPECT_EQ(output[0], 16);
    // All other coefficients should be 0
    for (int i = 1; i < 16; i++) {
        EXPECT_EQ(output[i], 0) << "Non-zero at index " << i;
    }
}

TEST_F(MotionMetricTest, HadamardCheckerboard) {
    // Checkerboard pattern: alternating +1/-1
    int16 input[16];
    for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
        input[r * 4 + c] = ((r + c) % 2 == 0) ? 1 : -1;

    int32 output[16];
    hadamard_4x4(input, 4, output);

    // DC should be 0 (equal +1 and -1)
    EXPECT_EQ(output[0], 0);
    // The alternating pattern in both dimensions maps to coefficient [1][1] = index 5
    // in the butterfly-ordered Hadamard (basis [1,-1,1,-1] × [1,-1,1,-1]).
    EXPECT_EQ(output[5], 16);
}

TEST_F(MotionMetricTest, SATDIdenticalBlocksIsZero) {
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = (i + j * 3) % 200;
        b.data_y[j * b.stride + i] = (i + j * 3) % 200;
    }
    EXPECT_EQ(compute_block_satd(a, b), 0);
}

TEST_F(MotionMetricTest, SATDDifferentBlocksPositive) {
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        a.data_y[j * a.stride + i] = 100;
        b.data_y[j * b.stride + i] = 110;
    }
    int32 satd = compute_block_satd(a, b);
    EXPECT_GT(satd, 0);
    // For uniform difference, all energy is in DC. The Hadamard DC coefficient
    // per 4x4 block is 16*10=160. After >>1 normalization: 80 per block, 16 blocks = 1280.
    // This is SAD/2 for pure-DC content (standard Hadamard normalization behavior).
    int32 sad = compute_block_sad(a, b);
    EXPECT_EQ(satd, sad / 2);
}
