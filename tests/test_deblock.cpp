#include <gtest/gtest.h>
#include <cstdlib>
#include "video/common.h"
#include "video/config.h"
#include "video/image.h"
#include "video/imageset.h"
#include "video/types.h"

using namespace evx;

// Forward declarations from deblock.cpp (internal functions)
namespace evx {
    evx_status deblock_image_filter(evx_block_desc *block_table, image_set *target_image);
    evx_status deblock_image(int16 macroblock_size, evx_block_desc *block_table, bool is_luma, image *target_image, int16 qp_offset);
    uint8 compute_deblock_strength(bool is_mb_boundary, const evx_block_desc &left, const evx_block_desc &right);
}

class DeblockTest : public ::testing::Test {
protected:
    static constexpr int WIDTH = 64;
    static constexpr int HEIGHT = 64;
    static constexpr int BLOCKS_W = WIDTH / EVX_MACROBLOCK_SIZE;
    static constexpr int BLOCKS_H = HEIGHT / EVX_MACROBLOCK_SIZE;

    image_set yuv;
    evx_block_desc block_table[BLOCKS_W * BLOCKS_H];

    void SetUp() override {
        yuv.initialize(EVX_IMAGE_FORMAT_R16S, WIDTH, HEIGHT);
        for (int i = 0; i < BLOCKS_W * BLOCKS_H; i++) {
            clear_block_desc(&block_table[i]);
            block_table[i].block_type = EVX_BLOCK_INTRA_DEFAULT;
        }
    }
};

TEST_F(DeblockTest, DoesNotCrashOnValidInput) {
    // Fill with a pattern that creates block boundaries
    image *y = yuv.query_y_image();
    int16 *data = reinterpret_cast<int16 *>(y->query_data());
    uint32 pitch = y->query_row_pitch() / sizeof(int16);

    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        // Create visible block boundaries
        int block_x = i / EVX_MACROBLOCK_SIZE;
        int block_y = j / EVX_MACROBLOCK_SIZE;
        data[j * pitch + i] = (block_x + block_y) * 50 + 20;
    }

    EXPECT_EQ(deblock_image_filter(block_table, &yuv), EVX_SUCCESS);
}

TEST_F(DeblockTest, SmoothImageUnchanged) {
    // A perfectly smooth image should not be significantly altered
    image *y = yuv.query_y_image();
    int16 *data = reinterpret_cast<int16 *>(y->query_data());
    uint32 pitch = y->query_row_pitch() / sizeof(int16);

    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        data[j * pitch + i] = 128;
    }

    // Save original
    std::vector<int16> original(WIDTH * HEIGHT);
    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        original[j * WIDTH + i] = data[j * pitch + i];
    }

    deblock_image_filter(block_table, &yuv);

    // Smooth image should be mostly unchanged
    int changed = 0;
    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        if (data[j * pitch + i] != original[j * WIDTH + i])
            changed++;
    }

    // At most boundary pixels should change
    EXPECT_LT(changed, WIDTH * HEIGHT / 2)
        << "Too many pixels changed in a smooth image";
}

TEST_F(DeblockTest, ReducesBlockBoundaryArtifacts) {
    // Create an image with sharp discontinuities at block boundaries
    image *y = yuv.query_y_image();
    int16 *data = reinterpret_cast<int16 *>(y->query_data());
    uint32 pitch = y->query_row_pitch() / sizeof(int16);

    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        int block_x = i / EVX_MACROBLOCK_SIZE;
        data[j * pitch + i] = (block_x % 2 == 0) ? 50 : 200;
    }

    // Measure discontinuity at boundary before deblock
    int boundary_x = EVX_MACROBLOCK_SIZE; // first vertical block boundary
    int64_t pre_discontinuity = 0;
    for (int j = 0; j < HEIGHT; j++) {
        pre_discontinuity += std::abs(data[j * pitch + boundary_x] - data[j * pitch + boundary_x - 1]);
    }

    deblock_image_filter(block_table, &yuv);

    // Measure discontinuity after deblock
    int64_t post_discontinuity = 0;
    for (int j = 0; j < HEIGHT; j++) {
        post_discontinuity += std::abs(data[j * pitch + boundary_x] - data[j * pitch + boundary_x - 1]);
    }

    EXPECT_LE(post_discontinuity, pre_discontinuity)
        << "Deblocking should reduce or maintain boundary discontinuity";
}

TEST_F(DeblockTest, CopyBlocksHandledCorrectly) {
    // Set some blocks as copy blocks - deblock should still work
    for (int i = 0; i < BLOCKS_W * BLOCKS_H; i++) {
        if (i % 2 == 0) {
            block_table[i].block_type = EVX_BLOCK_INTER_COPY;
        }
    }

    image *y = yuv.query_y_image();
    int16 *data = reinterpret_cast<int16 *>(y->query_data());
    uint32 pitch = y->query_row_pitch() / sizeof(int16);

    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        data[j * pitch + i] = 100;
    }

    EXPECT_EQ(deblock_image_filter(block_table, &yuv), EVX_SUCCESS);
}

TEST(DeblockStrengthTest, InternalBoundaryStrengthCappedAt1) {
    evx_block_desc left, right;
    clear_block_desc(&left);
    clear_block_desc(&right);
    left.block_type = EVX_BLOCK_INTRA_DEFAULT;
    right.block_type = EVX_BLOCK_INTRA_DEFAULT;

    // Internal boundary (same macroblock) → is_mb_boundary = false
    uint8 strength = compute_deblock_strength(false, left, right);
    EXPECT_EQ(strength, 1) << "Internal sub-block boundary should have strength 1";
}

TEST(DeblockStrengthTest, MacroblockBoundaryStrength2) {
    evx_block_desc left, right;
    clear_block_desc(&left);
    clear_block_desc(&right);
    left.block_type = EVX_BLOCK_INTRA_DEFAULT;
    right.block_type = EVX_BLOCK_INTER_DELTA;

    // Macroblock boundary → is_mb_boundary = true
    uint8 strength = compute_deblock_strength(true, left, right);
    EXPECT_EQ(strength, 2) << "Macroblock boundary should have strength 2";
}

TEST_F(DeblockTest, QPAdaptiveDeblockStrongerAtHighQP) {
    // Create two identical images with sharp block boundaries.
    // Deblock one with high-QP blocks and one with low-QP blocks.
    // The high-QP version should have smoother boundaries due to the
    // QP-adaptive alpha/beta offset for strength-2 edges.
    image_set yuv_high, yuv_low;
    yuv_high.initialize(EVX_IMAGE_FORMAT_R16S, WIDTH, HEIGHT);
    yuv_low.initialize(EVX_IMAGE_FORMAT_R16S, WIDTH, HEIGHT);

    evx_block_desc bt_high[BLOCKS_W * BLOCKS_H];
    evx_block_desc bt_low[BLOCKS_W * BLOCKS_H];

    for (int i = 0; i < BLOCKS_W * BLOCKS_H; i++) {
        clear_block_desc(&bt_high[i]);
        clear_block_desc(&bt_low[i]);
        bt_high[i].block_type = EVX_BLOCK_INTRA_DEFAULT;
        bt_low[i].block_type = EVX_BLOCK_INTRA_DEFAULT;
        bt_high[i].q_index = 20;
        bt_low[i].q_index = 4;
    }

    // Fill both images with identical block-boundary pattern.
    auto fill_pattern = [&](image_set &img) {
        image *y = img.query_y_image();
        int16 *data = reinterpret_cast<int16 *>(y->query_data());
        uint32 pitch = y->query_row_pitch() / sizeof(int16);
        for (int j = 0; j < HEIGHT; j++)
        for (int i = 0; i < WIDTH; i++) {
            int block_x = i / EVX_MACROBLOCK_SIZE;
            data[j * pitch + i] = (block_x % 2 == 0) ? 60 : 180;
        }
    };

    fill_pattern(yuv_high);
    fill_pattern(yuv_low);

    deblock_image_filter(bt_high, &yuv_high);
    deblock_image_filter(bt_low, &yuv_low);

    // Measure discontinuity at the first vertical block boundary.
    int boundary_x = EVX_MACROBLOCK_SIZE;
    image *y_high = yuv_high.query_y_image();
    image *y_low  = yuv_low.query_y_image();
    int16 *dh = reinterpret_cast<int16 *>(y_high->query_data());
    int16 *dl = reinterpret_cast<int16 *>(y_low->query_data());
    uint32 ph = y_high->query_row_pitch() / sizeof(int16);
    uint32 pl = y_low->query_row_pitch() / sizeof(int16);

    int64_t disc_high = 0, disc_low = 0;
    for (int j = 0; j < HEIGHT; j++) {
        disc_high += std::abs(dh[j * ph + boundary_x] - dh[j * ph + boundary_x - 1]);
        disc_low  += std::abs(dl[j * pl + boundary_x] - dl[j * pl + boundary_x - 1]);
    }

    EXPECT_LE(disc_high, disc_low)
        << "High-QP deblocking should produce smoother (or equal) boundaries than low-QP";
}

TEST_F(DeblockTest, ChromaDeblockingStronger) {
    // Create two identical image_sets. Deblock one with qp_offset=0 and one
    // with qp_offset=EVX_CHROMA_QP_OFFSET on the U chroma plane.
    // The offset version should produce smoother boundaries.
    image_set yuv_base, yuv_boost;
    yuv_base.initialize(EVX_IMAGE_FORMAT_R16S, WIDTH, HEIGHT);
    yuv_boost.initialize(EVX_IMAGE_FORMAT_R16S, WIDTH, HEIGHT);

    int16 chroma_mb = EVX_MACROBLOCK_SIZE >> 1;  // 8
    static constexpr int CW = WIDTH / 2;
    static constexpr int CH = HEIGHT / 2;

    // Fill U chroma planes with block-boundary pattern.
    auto fill_chroma = [&](image_set &img) {
        image *u = img.query_u_image();
        int16 *data = reinterpret_cast<int16 *>(u->query_data());
        uint32 pitch = u->query_row_pitch() / sizeof(int16);
        for (int j = 0; j < CH; j++)
        for (int i = 0; i < CW; i++) {
            int bx = i / chroma_mb;
            data[j * pitch + i] = (bx % 2 == 0) ? 60 : 180;
        }
    };

    fill_chroma(yuv_base);
    fill_chroma(yuv_boost);

    // Set moderate QP for all blocks.
    for (int i = 0; i < BLOCKS_W * BLOCKS_H; i++) {
        block_table[i].q_index = 12;
    }

    deblock_image(chroma_mb, block_table, false, yuv_base.query_u_image(), 0);
    deblock_image(chroma_mb, block_table, false, yuv_boost.query_u_image(), EVX_CHROMA_QP_OFFSET);

    // Measure discontinuity at chroma block boundary.
    int boundary_x = chroma_mb;
    image *u_base = yuv_base.query_u_image();
    image *u_boost = yuv_boost.query_u_image();
    int16 *db = reinterpret_cast<int16 *>(u_base->query_data());
    int16 *do_ = reinterpret_cast<int16 *>(u_boost->query_data());
    uint32 pb = u_base->query_row_pitch() / sizeof(int16);
    uint32 po = u_boost->query_row_pitch() / sizeof(int16);

    int64_t disc_base = 0, disc_boost = 0;
    for (int j = 0; j < CH; j++) {
        disc_base  += std::abs(db[j * pb + boundary_x] - db[j * pb + boundary_x - 1]);
        disc_boost += std::abs(do_[j * po + boundary_x] - do_[j * po + boundary_x - 1]);
    }

    EXPECT_LE(disc_boost, disc_base)
        << "Chroma with QP offset should have smoother (or equal) boundaries";
}
