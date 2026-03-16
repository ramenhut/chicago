#include <gtest/gtest.h>
#include "video/common.h"
#include "video/types.h"
#include "bitstream.h"
#include "abac.h"
#include "video/stream.h"
#include "video/config.h"

using namespace evx;

// Forward declarations for serialize/unserialize functions
namespace evx {
#if EVX_ENABLE_UNIFIED_METADATA
    evx_status serialize_block_table(uint32 block_count, uint32 width_in_blocks, evx_block_desc *block_table,
                                     bit_stream *feed_stream, entropy_coder *coder, evx_metadata_contexts *meta,
#if EVX_ENABLE_TEMPORAL_MVP
                                     const evx_block_desc *prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
                                     bool is_bidir_frame,
#endif
                                     bit_stream *output);
    evx_status unserialize_block_table(uint32 block_count, uint32 width_in_blocks, bit_stream *input,
                                       bit_stream *feed_stream, entropy_coder *coder, evx_metadata_contexts *meta,
#if EVX_ENABLE_TEMPORAL_MVP
                                       const evx_block_desc *prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
                                       bool is_bidir_frame,
#endif
                                       evx_block_desc *block_table);
#else
    evx_status serialize_block_table(uint32 block_count, uint32 width_in_blocks, evx_block_desc *block_table,
#if EVX_ENABLE_TEMPORAL_MVP
                                     const evx_block_desc *prev_block_table,
#endif
                                     bit_stream *feed_stream, entropy_coder *coder, bit_stream *output);
    evx_status unserialize_block_table(uint32 block_count, uint32 width_in_blocks,
#if EVX_ENABLE_TEMPORAL_MVP
                                       const evx_block_desc *prev_block_table,
#endif
                                       bit_stream *input,
                                       bit_stream *feed_stream, entropy_coder *coder, evx_block_desc *block_table);
#endif
}

class SerializeTest : public ::testing::Test {
protected:
    static constexpr int BLOCK_COUNT = 16; // 4x4 grid of macroblocks
    static constexpr uint32 WIDTH_IN_BLOCKS = 4;
    evx_block_desc blocks[BLOCK_COUNT];
    evx_block_desc decoded_blocks[BLOCK_COUNT];
    bit_stream output;
    bit_stream feed;
    entropy_coder coder;
#if EVX_ENABLE_UNIFIED_METADATA
    evx_metadata_contexts meta;
#endif

    void SetUp() override {
        output.resize_capacity(256 * 1024);
        feed.resize_capacity(64 * 1024);

        for (int i = 0; i < BLOCK_COUNT; i++) {
            clear_block_desc(&blocks[i]);
            clear_block_desc(&decoded_blocks[i]);
        }
    }

    evx_status do_serialize() {
        coder.clear();
#if EVX_ENABLE_UNIFIED_METADATA
        meta.clear();
        return serialize_block_table(BLOCK_COUNT, WIDTH_IN_BLOCKS, blocks, &feed, &coder, &meta,
#if EVX_ENABLE_TEMPORAL_MVP
            nullptr,
#endif
#if EVX_ENABLE_B_FRAMES
            false,  // is_bidir_frame — default to false for existing tests
#endif
            &output);
#else
        return serialize_block_table(BLOCK_COUNT, WIDTH_IN_BLOCKS, blocks,
#if EVX_ENABLE_TEMPORAL_MVP
            nullptr,
#endif
            &feed, &coder, &output);
#endif
    }

    evx_status do_unserialize() {
        coder.clear();
        coder.start_decode(&output);
#if EVX_ENABLE_UNIFIED_METADATA
        meta.clear();
        return unserialize_block_table(BLOCK_COUNT, WIDTH_IN_BLOCKS, &output, &feed, &coder, &meta,
#if EVX_ENABLE_TEMPORAL_MVP
            nullptr,
#endif
#if EVX_ENABLE_B_FRAMES
            false,  // is_bidir_frame — default to false for existing tests
#endif
            decoded_blocks);
#else
        return unserialize_block_table(BLOCK_COUNT, WIDTH_IN_BLOCKS,
#if EVX_ENABLE_TEMPORAL_MVP
            nullptr,
#endif
            &output, &feed, &coder, decoded_blocks);
#endif
    }

#if EVX_ENABLE_B_FRAMES
    evx_status do_serialize_bidir() {
        coder.clear();
        meta.clear();
        return serialize_block_table(BLOCK_COUNT, WIDTH_IN_BLOCKS, blocks, &feed, &coder, &meta,
#if EVX_ENABLE_TEMPORAL_MVP
            nullptr,
#endif
            true,  // is_bidir_frame
            &output);
    }

    evx_status do_unserialize_bidir() {
        coder.clear();
        coder.start_decode(&output);
        meta.clear();
        return unserialize_block_table(BLOCK_COUNT, WIDTH_IN_BLOCKS, &output, &feed, &coder, &meta,
#if EVX_ENABLE_TEMPORAL_MVP
            nullptr,
#endif
            true,  // is_bidir_frame
            decoded_blocks);
    }
#endif
};

TEST_F(SerializeTest, BlockTableRoundtripIntraDefault) {
    // All blocks are INTRA_DEFAULT with various QP values
    for (int i = 0; i < BLOCK_COUNT; i++) {
        blocks[i].block_type = EVX_BLOCK_INTRA_DEFAULT;
        blocks[i].q_index = i % 8;
    }

    EXPECT_EQ(do_serialize(), EVX_SUCCESS);
    coder.finish_encode(&output);

    EXPECT_EQ(do_unserialize(), EVX_SUCCESS);

    for (int i = 0; i < BLOCK_COUNT; i++) {
        EXPECT_EQ(decoded_blocks[i].block_type, blocks[i].block_type)
            << "Block type mismatch at index " << i;
        EXPECT_EQ(decoded_blocks[i].q_index, blocks[i].q_index)
            << "QP mismatch at index " << i;
    }
}

TEST_F(SerializeTest, BlockTableRoundtripWithMotion) {
    // Mix of block types with motion vectors
    blocks[0].block_type = EVX_BLOCK_INTRA_DEFAULT;
    blocks[0].q_index = 4;

    blocks[1].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
    blocks[1].motion_x = 3;
    blocks[1].motion_y = -2;
    blocks[1].sp_pred = false;
    blocks[1].q_index = 5;

    blocks[2].block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    blocks[2].motion_x = -5;
    blocks[2].motion_y = 7;
    blocks[2].sp_pred = true;
    blocks[2].sp_amount = false; // half pixel
    blocks[2].sp_index = 3;
    blocks[2].prediction_target = 1;
    blocks[2].q_index = 6;

    blocks[3].block_type = EVX_BLOCK_INTER_COPY;

    // Rest are intra default
    for (int i = 4; i < BLOCK_COUNT; i++) {
        blocks[i].block_type = EVX_BLOCK_INTRA_DEFAULT;
        blocks[i].q_index = 2;
    }

    EXPECT_EQ(do_serialize(), EVX_SUCCESS);
    coder.finish_encode(&output);

    EXPECT_EQ(do_unserialize(), EVX_SUCCESS);

    for (int i = 0; i < BLOCK_COUNT; i++) {
        EXPECT_EQ(decoded_blocks[i].block_type, blocks[i].block_type)
            << "Block type mismatch at " << i;

        if (EVX_IS_MOTION_BLOCK_TYPE(blocks[i].block_type)) {
            EXPECT_EQ(decoded_blocks[i].motion_x, blocks[i].motion_x)
                << "Motion X mismatch at " << i;
            EXPECT_EQ(decoded_blocks[i].motion_y, blocks[i].motion_y)
                << "Motion Y mismatch at " << i;
            EXPECT_EQ(decoded_blocks[i].sp_pred, blocks[i].sp_pred)
                << "SP pred mismatch at " << i;

            if (blocks[i].sp_pred) {
                EXPECT_EQ(decoded_blocks[i].sp_amount, blocks[i].sp_amount)
                    << "SP amount mismatch at " << i;
                EXPECT_EQ(decoded_blocks[i].sp_index, blocks[i].sp_index)
                    << "SP index mismatch at " << i;
            }
        }

        if (!EVX_IS_COPY_BLOCK_TYPE(blocks[i].block_type)) {
            EXPECT_EQ(decoded_blocks[i].q_index, blocks[i].q_index)
                << "QP mismatch at " << i;
        }
    }
}

TEST_F(SerializeTest, AllCopyBlocksRoundtrip) {
    for (int i = 0; i < BLOCK_COUNT; i++) {
        blocks[i].block_type = EVX_BLOCK_INTER_COPY;
    }

    EXPECT_EQ(do_serialize(), EVX_SUCCESS);
    coder.finish_encode(&output);

    EXPECT_EQ(do_unserialize(), EVX_SUCCESS);

    for (int i = 0; i < BLOCK_COUNT; i++) {
        EXPECT_EQ(decoded_blocks[i].block_type, EVX_BLOCK_INTER_COPY);
    }
}

TEST_F(SerializeTest, SubpixelMotionRoundtrip) {
    // All blocks have motion with subpixel
    for (int i = 0; i < BLOCK_COUNT; i++) {
        blocks[i].block_type = EVX_BLOCK_INTRA_MOTION_DELTA;
        blocks[i].motion_x = (i % 5) - 2;
        blocks[i].motion_y = (i % 3) - 1;
        blocks[i].sp_pred = true;
        blocks[i].sp_amount = (i % 2);        // alternating half/quarter
        blocks[i].sp_index = i % 8;           // 3-bit direction
        blocks[i].q_index = 4;
    }

    EXPECT_EQ(do_serialize(), EVX_SUCCESS);
    coder.finish_encode(&output);

    EXPECT_EQ(do_unserialize(), EVX_SUCCESS);

    for (int i = 0; i < BLOCK_COUNT; i++) {
        EXPECT_EQ(decoded_blocks[i].sp_pred, blocks[i].sp_pred) << "at " << i;
        EXPECT_EQ(decoded_blocks[i].sp_amount, blocks[i].sp_amount) << "at " << i;
        EXPECT_EQ(decoded_blocks[i].sp_index, blocks[i].sp_index) << "at " << i;
    }
}

#if EVX_ENABLE_B_FRAMES
TEST_F(SerializeTest, BFrameBlockRoundtrip) {
    blocks[0].block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    blocks[0].prediction_mode = 0;  // forward
    blocks[0].prediction_target = 0;
    blocks[0].motion_x = 3;
    blocks[0].motion_y = -1;
    blocks[0].sp_pred = false;
    blocks[0].q_index = 5;

    blocks[1].block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    blocks[1].prediction_mode = 1;  // backward
    blocks[1].prediction_target = 1;
    blocks[1].motion_x = -2;
    blocks[1].motion_y = 4;
    blocks[1].sp_pred = true;
    blocks[1].sp_amount = true;
    blocks[1].sp_index = 5;
    blocks[1].q_index = 6;

    blocks[2].block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    blocks[2].prediction_mode = 2;  // bi-predicted
    blocks[2].prediction_target = 0;
    blocks[2].motion_x = 1;
    blocks[2].motion_y = 2;
    blocks[2].sp_pred = false;
    blocks[2].prediction_target_b = 1;
    blocks[2].motion_x_b = -3;
    blocks[2].motion_y_b = 1;
    blocks[2].sp_pred_b = true;
    blocks[2].sp_amount_b = false;
    blocks[2].sp_index_b = 2;
    blocks[2].q_index = 4;

    blocks[3].block_type = EVX_BLOCK_INTRA_DEFAULT;
    blocks[3].intra_mode = 3;
    blocks[3].q_index = 7;

    for (int i = 4; i < BLOCK_COUNT; i++) {
        blocks[i].block_type = EVX_BLOCK_INTER_COPY;
        blocks[i].prediction_mode = 0;
        blocks[i].prediction_target = 0;
    }

    EXPECT_EQ(do_serialize_bidir(), EVX_SUCCESS);
    coder.finish_encode(&output);

    EXPECT_EQ(do_unserialize_bidir(), EVX_SUCCESS);

    for (int i = 0; i < BLOCK_COUNT; i++) {
        EXPECT_EQ(decoded_blocks[i].block_type, blocks[i].block_type)
            << "Block type mismatch at " << i;

        if (!EVX_IS_INTRA_BLOCK_TYPE(blocks[i].block_type) &&
            !EVX_IS_COPY_BLOCK_TYPE(blocks[i].block_type)) {
            EXPECT_EQ(decoded_blocks[i].prediction_mode, blocks[i].prediction_mode)
                << "Prediction mode mismatch at " << i;
        }

        if (blocks[i].prediction_mode == 2 &&
            !EVX_IS_COPY_BLOCK_TYPE(blocks[i].block_type)) {
            EXPECT_EQ(decoded_blocks[i].prediction_target_b, blocks[i].prediction_target_b)
                << "Backward ref mismatch at " << i;
            EXPECT_EQ(decoded_blocks[i].motion_x_b, blocks[i].motion_x_b)
                << "Motion X B mismatch at " << i;
            EXPECT_EQ(decoded_blocks[i].motion_y_b, blocks[i].motion_y_b)
                << "Motion Y B mismatch at " << i;
            EXPECT_EQ(decoded_blocks[i].sp_pred_b, blocks[i].sp_pred_b)
                << "SP pred B mismatch at " << i;
            if (blocks[i].sp_pred_b) {
                EXPECT_EQ(decoded_blocks[i].sp_amount_b, blocks[i].sp_amount_b)
                    << "SP amount B mismatch at " << i;
                EXPECT_EQ(decoded_blocks[i].sp_index_b, blocks[i].sp_index_b)
                    << "SP index B mismatch at " << i;
            }
        }
    }
}
#endif // EVX_ENABLE_B_FRAMES
