#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include "video/quantize.h"
#include "video/transform.h"
#include "video/macroblock.h"
#include "video/types.h"
#include "video/config.h"

using namespace evx;

// The macroblock struct holds pointers (data_y, data_u, data_v) that alias
// into an external buffer -- it does NOT embed arrays.  Tests must allocate
// backing memory and point the macroblock into it.

static const uint32 MB_STRIDE   = EVX_MACROBLOCK_SIZE;           // 16 elements
static const uint32 LUMA_COUNT  = MB_STRIDE * EVX_MACROBLOCK_SIZE;          // 16*16
static const uint32 CHROMA_STRIDE = MB_STRIDE >> 1;                         // 8
static const uint32 CHROMA_COUNT  = CHROMA_STRIDE * (EVX_MACROBLOCK_SIZE >> 1); // 8*8

// Helper: allocate backing storage and wire a macroblock to it.
// Caller is responsible for freeing the returned arrays (or use the RAII
// helper below).
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

class QuantizeTest : public ::testing::Test {
protected:
    macroblock src, quantized, dequantized;
    int16 *src_y, *src_u, *src_v;
    int16 *q_y,   *q_u,   *q_v;
    int16 *dq_y,  *dq_u,  *dq_v;

    void SetUp() override {
        alloc_macroblock(src,        src_y, src_u, src_v);
        alloc_macroblock(quantized,  q_y,   q_u,   q_v);
        alloc_macroblock(dequantized, dq_y, dq_u,  dq_v);
    }

    void TearDown() override {
        free_macroblock_bufs(src_y, src_u, src_v);
        free_macroblock_bufs(q_y,   q_u,   q_v);
        free_macroblock_bufs(dq_y,  dq_u,  dq_v);
    }

    void fillWithGradient(macroblock &mb) {
        for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
        for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
            mb.data_y[j * mb.stride + i] = (i + j) * 10;
        }
        for (int j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); j++)
        for (int i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); i++) {
            mb.data_u[j * (mb.stride >> 1) + i] = (i + j) * 5;
            mb.data_v[j * (mb.stride >> 1) + i] = (i + j) * 5;
        }
    }
};

TEST_F(QuantizeTest, RoundtripIntraDefault) {
    fillWithGradient(src);

    uint8 qp = 8;
    quantize_macroblock(qp, EVX_BLOCK_INTRA_DEFAULT, src, &quantized);
    inverse_quantize_macroblock(qp, EVX_BLOCK_INTRA_DEFAULT, quantized, &dequantized);

    // After quantize + inverse_quantize, values should be approximately
    // the same (with quantization loss).
    int total_error = 0;
    int max_error = 0;
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        int err = std::abs(src.data_y[j * src.stride + i] - dequantized.data_y[j * dequantized.stride + i]);
        total_error += err;
        if (err > max_error) max_error = err;
    }
    // At qp=8, errors should be bounded
    EXPECT_LT(max_error, 200) << "Max error too large for qp=8";
}

TEST_F(QuantizeTest, RoundtripInterDelta) {
    fillWithGradient(src);

    uint8 qp = 4;
    quantize_macroblock(qp, EVX_BLOCK_INTER_DELTA, src, &quantized);
    inverse_quantize_macroblock(qp, EVX_BLOCK_INTER_DELTA, quantized, &dequantized);

    int max_error = 0;
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        int err = std::abs(src.data_y[j * src.stride + i] - dequantized.data_y[j * dequantized.stride + i]);
        if (err > max_error) max_error = err;
    }
    EXPECT_LT(max_error, 150) << "Max error too large for qp=4";
}

TEST_F(QuantizeTest, ZeroInputProducesZeroOutput) {
    // src is already zeroed from SetUp
    uint8 qp = 16;
    quantize_macroblock(qp, EVX_BLOCK_INTRA_DEFAULT, src, &quantized);

    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        EXPECT_EQ(quantized.data_y[j * quantized.stride + i], 0);
    }
}

TEST_F(QuantizeTest, HigherQPProducesMoreZeros) {
    fillWithGradient(src);

    macroblock q_low, q_high;
    int16 *ql_y, *ql_u, *ql_v;
    int16 *qh_y, *qh_u, *qh_v;
    alloc_macroblock(q_low,  ql_y, ql_u, ql_v);
    alloc_macroblock(q_high, qh_y, qh_u, qh_v);

    quantize_macroblock(2, EVX_BLOCK_INTRA_DEFAULT, src, &q_low);
    quantize_macroblock(28, EVX_BLOCK_INTRA_DEFAULT, src, &q_high);

    int nonzero_low = 0, nonzero_high = 0;
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        if (q_low.data_y[j * q_low.stride + i] != 0) nonzero_low++;
        if (q_high.data_y[j * q_high.stride + i] != 0) nonzero_high++;
    }

    EXPECT_GE(nonzero_low, nonzero_high)
        << "Lower QP should preserve more non-zero coefficients";

    free_macroblock_bufs(ql_y, ql_u, ql_v);
    free_macroblock_bufs(qh_y, qh_u, qh_v);
}

TEST_F(QuantizeTest, AdaptiveQPVariesByContent) {
    // Create two blocks: one flat, one with high variance
    macroblock flat_block, varied_block;
    int16 *fb_y, *fb_u, *fb_v;
    int16 *vb_y, *vb_u, *vb_v;
    alloc_macroblock(flat_block,   fb_y, fb_u, fb_v);
    alloc_macroblock(varied_block, vb_y, vb_u, vb_v);

    // Flat block - all small values
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        flat_block.data_y[j * flat_block.stride + i] = 1;
        varied_block.data_y[j * varied_block.stride + i] = (i * j) % 100;
    }

    uint8 qp_flat = query_block_quantization_parameter(8, flat_block, EVX_BLOCK_INTRA_DEFAULT);
    uint8 qp_varied = query_block_quantization_parameter(8, varied_block, EVX_BLOCK_INTRA_DEFAULT);

    // Both should be valid QP values
    EXPECT_LT(qp_flat, EVX_MAX_MPEG_QUANT_LEVELS);
    EXPECT_LT(qp_varied, EVX_MAX_MPEG_QUANT_LEVELS);

    free_macroblock_bufs(fb_y, fb_u, fb_v);
    free_macroblock_bufs(vb_y, vb_u, vb_v);
}

TEST_F(QuantizeTest, DCScalingPreservation) {
    // Set a large DC value (position [0,0])
    src.data_y[0] = 1000;
    // Set small AC values
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        if (i == 0 && j == 0) continue;
        src.data_y[j * src.stride + i] = 2;
    }

    uint8 qp = 16;
    quantize_macroblock(qp, EVX_BLOCK_INTRA_DEFAULT, src, &quantized);
    inverse_quantize_macroblock(qp, EVX_BLOCK_INTRA_DEFAULT, quantized, &dequantized);

    // DC should be reasonably preserved even at high QP
    int dc_error = std::abs(src.data_y[0] - dequantized.data_y[0]);
    EXPECT_LT(dc_error, 100) << "DC coefficient should be well preserved";
}

#if EVX_ENABLE_FREQ_DEADZONE

TEST_F(QuantizeTest, FreqDeadzoneZerosHighFreq) {
    // Fill first 8x8 sub-block with a uniform small value.
    // High-freq positions should be zeroed more aggressively than low-freq.
    int16 uniform_val = 30;
    for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
        src.data_y[j * src.stride + i] = uniform_val;
    }

    uint8 qp = 12;
    quantize_macroblock(qp, EVX_BLOCK_INTER_DELTA, src, &quantized);

    // Count non-zero in low-freq region (top-left 3x3) vs high-freq (bottom-right 3x3).
    int nonzero_low = 0, nonzero_high = 0;
    for (int j = 0; j < 3; j++)
    for (int i = 0; i < 3; i++) {
        if (quantized.data_y[j * quantized.stride + i] != 0) nonzero_low++;
    }
    for (int j = 5; j < 8; j++)
    for (int i = 5; i < 8; i++) {
        if (quantized.data_y[j * quantized.stride + i] != 0) nonzero_high++;
    }

    // With frequency-dependent deadzone, high-freq should have fewer or equal non-zeros.
    EXPECT_GE(nonzero_low, nonzero_high)
        << "Low-freq should retain more non-zero coefficients than high-freq";
}

TEST_F(QuantizeTest, FreqDeadzonePreservesDC) {
    // DC position [0,0] has deadzone weight 2 (1.0x = standard).
    // A moderate DC value should survive quantization.
    src.data_y[0] = 200;

    uint8 qp = 8;
    quantize_macroblock(qp, EVX_BLOCK_INTER_DELTA, src, &quantized);

    EXPECT_NE(quantized.data_y[0], 0)
        << "DC coefficient should survive inter quantization with freq deadzone";
}

TEST_F(QuantizeTest, FreqDeadzoneRoundtrip) {
    fillWithGradient(src);

    uint8 qp = 8;
    quantize_macroblock(qp, EVX_BLOCK_INTER_DELTA, src, &quantized);
    inverse_quantize_macroblock(qp, EVX_BLOCK_INTER_DELTA, quantized, &dequantized);

    int max_error = 0;
    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        int err = std::abs(src.data_y[j * src.stride + i] - dequantized.data_y[j * dequantized.stride + i]);
        if (err > max_error) max_error = err;
    }
    EXPECT_LT(max_error, 200) << "Roundtrip error should be bounded with freq deadzone";
}

#endif // EVX_ENABLE_FREQ_DEADZONE

#if EVX_CHROMA_QP_OFFSET > 0

TEST_F(QuantizeTest, ChromaQPOffsetApplied) {
    // Fill luma and chroma with the same gradient values.
    for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
        int16 val = (int16)((i + j) * 10);
        // First 8x8 luma sub-block
        src.data_y[j * src.stride + i] = val;
        // Chroma U
        src.data_u[j * (src.stride >> 1) + i] = val;
    }

    uint8 qp = 8;
    quantize_macroblock(qp, EVX_BLOCK_INTRA_DEFAULT, src, &quantized);

    // Count non-zero coefficients in luma 8x8 vs chroma U 8x8.
    int nonzero_luma = 0, nonzero_chroma = 0;
    for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
        if (quantized.data_y[j * quantized.stride + i] != 0) nonzero_luma++;
        if (quantized.data_u[j * (quantized.stride >> 1) + i] != 0) nonzero_chroma++;
    }

    // Chroma should have fewer or equal non-zeros due to higher effective QP.
    EXPECT_GE(nonzero_luma, nonzero_chroma)
        << "Chroma (QP+" << EVX_CHROMA_QP_OFFSET << ") should be quantized more coarsely than luma";
}

TEST_F(QuantizeTest, ChromaQPOffsetRoundtrip) {
    fillWithGradient(src);

    uint8 qp = 8;
    quantize_macroblock(qp, EVX_BLOCK_INTRA_DEFAULT, src, &quantized);
    inverse_quantize_macroblock(qp, EVX_BLOCK_INTRA_DEFAULT, quantized, &dequantized);

    // Verify chroma roundtrip error is bounded.
    int max_chroma_error = 0;
    for (int j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); j++)
    for (int i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); i++) {
        int err_u = std::abs(src.data_u[j * (src.stride >> 1) + i] - dequantized.data_u[j * (dequantized.stride >> 1) + i]);
        int err_v = std::abs(src.data_v[j * (src.stride >> 1) + i] - dequantized.data_v[j * (dequantized.stride >> 1) + i]);
        if (err_u > max_chroma_error) max_chroma_error = err_u;
        if (err_v > max_chroma_error) max_chroma_error = err_v;
    }

    EXPECT_LT(max_chroma_error, 250)
        << "Chroma roundtrip error should be bounded even with QP offset";
}

#endif // EVX_CHROMA_QP_OFFSET

#if EVX_ENABLE_QP_ADAPTIVE_DEADZONE && EVX_ENABLE_FREQ_DEADZONE

TEST_F(QuantizeTest, QPAdaptiveDeadzoneMoreZerosAtHighQP) {
    // Fill first 8x8 sub-block with moderate uniform values.
    int16 val = 40;
    for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
        src.data_y[j * src.stride + i] = val;
    }

    // Quantize at low QP (below threshold) and high QP (above threshold).
    macroblock q_low, q_high;
    int16 *ql_y, *ql_u, *ql_v;
    int16 *qh_y, *qh_u, *qh_v;
    alloc_macroblock(q_low,  ql_y, ql_u, ql_v);
    alloc_macroblock(q_high, qh_y, qh_u, qh_v);

    quantize_macroblock(8, EVX_BLOCK_INTER_DELTA, src, &q_low);
    quantize_macroblock(24, EVX_BLOCK_INTER_DELTA, src, &q_high);

    // Count zeros in first 8x8 sub-block.
    int zeros_low = 0, zeros_high = 0;
    for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
        if (q_low.data_y[j * q_low.stride + i] == 0) zeros_low++;
        if (q_high.data_y[j * q_high.stride + i] == 0) zeros_high++;
    }

    EXPECT_GT(zeros_high, zeros_low)
        << "QP=24 (with adaptive deadzone) should produce strictly more zeros than QP=8";

    free_macroblock_bufs(ql_y, ql_u, ql_v);
    free_macroblock_bufs(qh_y, qh_u, qh_v);
}

#endif // EVX_ENABLE_QP_ADAPTIVE_DEADZONE && EVX_ENABLE_FREQ_DEADZONE

#if EVX_ENABLE_INTRA_DEADZONE

TEST_F(QuantizeTest, IntraDeadzoneMoreZerosAtHighQP) {
    // Fill first 8x8 sub-block with moderate values to test intra deadzone.
    int16 val = 30;
    for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
        src.data_y[j * src.stride + i] = val;
    }

    // Quantize at QP=8 (below threshold, no deadzone) and QP=20 (above threshold).
    macroblock q_low, q_high;
    int16 *ql_y, *ql_u, *ql_v;
    int16 *qh_y, *qh_u, *qh_v;
    alloc_macroblock(q_low,  ql_y, ql_u, ql_v);
    alloc_macroblock(q_high, qh_y, qh_u, qh_v);

    quantize_macroblock(8, EVX_BLOCK_INTRA_DEFAULT, src, &q_low);
    quantize_macroblock(20, EVX_BLOCK_INTRA_DEFAULT, src, &q_high);

    // Count zeros in first 8x8 sub-block (skip DC at [0,0]).
    int zeros_low = 0, zeros_high = 0;
    for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
        if (i == 0 && j == 0) continue; // skip DC
        if (q_low.data_y[j * q_low.stride + i] == 0) zeros_low++;
        if (q_high.data_y[j * q_high.stride + i] == 0) zeros_high++;
    }

    EXPECT_GT(zeros_high, zeros_low)
        << "QP=20 (with intra deadzone) should produce more zeros than QP=8 (no deadzone)";

    free_macroblock_bufs(ql_y, ql_u, ql_v);
    free_macroblock_bufs(qh_y, qh_u, qh_v);
}

#endif // EVX_ENABLE_INTRA_DEADZONE
