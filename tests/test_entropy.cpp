#include <gtest/gtest.h>
#include "abac.h"
#include "golomb.h"
#include "video/stream.h"
#include "bitstream.h"
#include "video/scan.h"
#include "video/config.h"

using namespace evx;

// ---------------------------------------------------------------------------
// ABAC encode/decode roundtrip
// ---------------------------------------------------------------------------

TEST(ABAC, BasicRoundtrip) {
    bit_stream source;
    source.resize_capacity(64 * 1024);

    // Write a known bit pattern: 1 0 1 1 0 0 1 0
    uint8 pattern[] = {1, 0, 1, 1, 0, 0, 1, 0};
    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(source.write_bit(pattern[i]), EVX_SUCCESS);
    }

    uint32 symbol_count = source.query_occupancy();

    bit_stream encoded;
    encoded.resize_capacity(64 * 1024);

    entropy_coder coder;
    ASSERT_EQ(coder.encode(&source, &encoded), EVX_SUCCESS);

    bit_stream decoded;
    decoded.resize_capacity(64 * 1024);

    entropy_coder decoder;
    ASSERT_EQ(decoder.decode(symbol_count, &encoded, &decoded), EVX_SUCCESS);

    for (int i = 0; i < 8; i++) {
        uint8 bit = 0;
        ASSERT_EQ(decoded.read_bit(&bit), EVX_SUCCESS);
        EXPECT_EQ(bit, pattern[i]) << "Mismatch at bit " << i;
    }
}

TEST(ABAC, AllZeros) {
    bit_stream source;
    source.resize_capacity(64 * 1024);

    const int count = 64;
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(source.write_bit(0), EVX_SUCCESS);
    }

    uint32 symbol_count = source.query_occupancy();

    bit_stream encoded;
    encoded.resize_capacity(64 * 1024);

    entropy_coder coder;
    ASSERT_EQ(coder.encode(&source, &encoded), EVX_SUCCESS);

    bit_stream decoded;
    decoded.resize_capacity(64 * 1024);

    entropy_coder decoder;
    ASSERT_EQ(decoder.decode(symbol_count, &encoded, &decoded), EVX_SUCCESS);

    for (int i = 0; i < count; i++) {
        uint8 bit = 0;
        ASSERT_EQ(decoded.read_bit(&bit), EVX_SUCCESS);
        EXPECT_EQ(bit, 0u) << "Expected 0 at bit " << i;
    }
}

TEST(ABAC, AllOnes) {
    bit_stream source;
    source.resize_capacity(64 * 1024);

    const int count = 64;
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(source.write_bit(1), EVX_SUCCESS);
    }

    uint32 symbol_count = source.query_occupancy();

    bit_stream encoded;
    encoded.resize_capacity(64 * 1024);

    entropy_coder coder;
    ASSERT_EQ(coder.encode(&source, &encoded), EVX_SUCCESS);

    bit_stream decoded;
    decoded.resize_capacity(64 * 1024);

    entropy_coder decoder;
    ASSERT_EQ(decoder.decode(symbol_count, &encoded, &decoded), EVX_SUCCESS);

    for (int i = 0; i < count; i++) {
        uint8 bit = 0;
        ASSERT_EQ(decoded.read_bit(&bit), EVX_SUCCESS);
        EXPECT_EQ(bit, 1u) << "Expected 1 at bit " << i;
    }
}

TEST(ABAC, Alternating) {
    bit_stream source;
    source.resize_capacity(64 * 1024);

    const int count = 128;
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(source.write_bit(i % 2), EVX_SUCCESS);
    }

    uint32 symbol_count = source.query_occupancy();

    bit_stream encoded;
    encoded.resize_capacity(64 * 1024);

    entropy_coder coder;
    ASSERT_EQ(coder.encode(&source, &encoded), EVX_SUCCESS);

    bit_stream decoded;
    decoded.resize_capacity(64 * 1024);

    entropy_coder decoder;
    ASSERT_EQ(decoder.decode(symbol_count, &encoded, &decoded), EVX_SUCCESS);

    for (int i = 0; i < count; i++) {
        uint8 bit = 0;
        ASSERT_EQ(decoded.read_bit(&bit), EVX_SUCCESS);
        EXPECT_EQ(bit, (uint8)(i % 2)) << "Mismatch at bit " << i;
    }
}

TEST(ABAC, PseudoRandom) {
    bit_stream source;
    source.resize_capacity(64 * 1024);

    // Pseudo-random sequence via simple LCG
    const int count = 256;
    uint8 expected[256];
    uint32 seed = 12345;
    for (int i = 0; i < count; i++) {
        seed = seed * 1103515245 + 12345;
        expected[i] = (seed >> 16) & 1;
        ASSERT_EQ(source.write_bit(expected[i]), EVX_SUCCESS);
    }

    uint32 symbol_count = source.query_occupancy();

    bit_stream encoded;
    encoded.resize_capacity(64 * 1024);

    entropy_coder coder;
    ASSERT_EQ(coder.encode(&source, &encoded), EVX_SUCCESS);

    bit_stream decoded;
    decoded.resize_capacity(64 * 1024);

    entropy_coder decoder;
    ASSERT_EQ(decoder.decode(symbol_count, &encoded, &decoded), EVX_SUCCESS);

    for (int i = 0; i < count; i++) {
        uint8 bit = 0;
        ASSERT_EQ(decoded.read_bit(&bit), EVX_SUCCESS);
        EXPECT_EQ(bit, expected[i]) << "Mismatch at bit " << i;
    }
}

// ---------------------------------------------------------------------------
// Golomb unsigned encode/decode roundtrip
// ---------------------------------------------------------------------------

TEST(Golomb, UnsignedRoundtrip) {
    uint16 test_values[] = {0, 1, 2, 5, 100, 255};

    for (int i = 0; i < 6; i++) {
        uint8 encode_count = 0;
        uint32 code = encode_unsigned_golomb_value(test_values[i], &encode_count);

        uint8 decode_count = 0;
        uint16 decoded = decode_unsigned_golomb_value(code, &decode_count);

        EXPECT_EQ(decoded, test_values[i])
            << "Unsigned Golomb roundtrip failed for value " << test_values[i];
        EXPECT_EQ(encode_count, decode_count)
            << "Bit count mismatch for value " << test_values[i];
    }
}

// ---------------------------------------------------------------------------
// Golomb signed encode/decode roundtrip
// ---------------------------------------------------------------------------

TEST(Golomb, SignedRoundtrip) {
    int16 test_values[] = {-100, -1, 0, 1, 100};

    for (int i = 0; i < 5; i++) {
        uint8 encode_count = 0;
        uint32 code = encode_signed_golomb_value(test_values[i], &encode_count);

        uint8 decode_count = 0;
        int16 decoded = decode_signed_golomb_value(code, &decode_count);

        EXPECT_EQ(decoded, test_values[i])
            << "Signed Golomb roundtrip failed for value " << test_values[i];
        EXPECT_EQ(encode_count, decode_count)
            << "Bit count mismatch for value " << test_values[i];
    }
}

// ---------------------------------------------------------------------------
// Stream encode/decode unsigned value roundtrip
// ---------------------------------------------------------------------------

TEST(Stream, UnsignedValueRoundtrip) {
    // stream_decode_value now correctly uses query_occupancy() (bits) for peek_bits.
    uint16 test_values[] = {0, 1, 2, 10, 127, 255, 1000, 5000};
    const int count = 8;

    bit_stream bs;
    bs.resize_capacity(64 * 1024);

    for (int i = 0; i < count; i++) {
        ASSERT_EQ(stream_encode_value(test_values[i], &bs), EVX_SUCCESS);
    }

    for (int i = 0; i < count; i++) {
        uint16 decoded = 0;
        ASSERT_EQ(stream_decode_value(&bs, &decoded), EVX_SUCCESS)
            << "Decode failed at index " << i;
        EXPECT_EQ(decoded, test_values[i])
            << "Unsigned stream roundtrip failed for value " << test_values[i];
    }
}

// ---------------------------------------------------------------------------
// Stream encode/decode signed value roundtrip
// ---------------------------------------------------------------------------

TEST(Stream, SignedValueRoundtrip) {
    // With B1 (Golomb bit count) and B2 (query_occupancy) fixed, signed values
    // can now be sequentially decoded from a single stream.
    int16 test_values[] = {-5000, -100, -1, 0, 1, 100, 5000};
    const int count = 7;

    bit_stream bs;
    bs.resize_capacity(64 * 1024);

    for (int i = 0; i < count; i++) {
        ASSERT_EQ(stream_encode_value(test_values[i], &bs), EVX_SUCCESS);
    }

    for (int i = 0; i < count; i++) {
        int16 decoded = 0;
        ASSERT_EQ(stream_decode_value(&bs, &decoded), EVX_SUCCESS)
            << "Decode failed for value " << test_values[i];
        EXPECT_EQ(decoded, test_values[i])
            << "Signed stream roundtrip failed for value " << test_values[i];
    }
}

// ---------------------------------------------------------------------------
// Entropy stream encode/decode roundtrip (incremental mode)
// ---------------------------------------------------------------------------

TEST(EntropyStream, SignedValueRoundtrip) {
    int16 test_values[] = {0, 5, -3, 127, -100, 1, 0, -1};
    const int count = 8;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    // Encode
    entropy_coder coder;
    coder.clear();
    feed_stream.empty();

    for (int i = 0; i < count; i++) {
        ASSERT_EQ(entropy_stream_encode_value(test_values[i], &feed_stream, &coder, &output),
                  EVX_SUCCESS)
            << "Encode failed at index " << i;
    }
    ASSERT_EQ(coder.finish_encode(&output), EVX_SUCCESS);

    // Decode
    entropy_coder decoder;
    decoder.clear();
    feed_stream.empty();

    ASSERT_EQ(decoder.start_decode(&output), EVX_SUCCESS);

    for (int i = 0; i < count; i++) {
        int16 result = 0;
        ASSERT_EQ(entropy_stream_decode_value(&output, &decoder, &feed_stream, &result),
                  EVX_SUCCESS)
            << "Decode failed at index " << i;
        EXPECT_EQ(result, test_values[i])
            << "Entropy stream roundtrip mismatch at index " << i;
    }
}

// ---------------------------------------------------------------------------
// RLE 8x8 block encode/decode roundtrip
// ---------------------------------------------------------------------------

TEST(EntropyStream, RLE8x8Roundtrip) {
    // Create a sparse 64-element block typical of DCT output:
    // a few non-zero values in the low-frequency positions, zeros elsewhere.
    int16 input[64] = {};
    input[0] = 120;   // DC coefficient
    input[1] = -15;
    input[2] = 7;
    input[8] = -3;
    input[9] = 2;
    input[16] = 1;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    // Encode
    entropy_coder coder;
    coder.clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_encode_8x8(input, &feed_stream, &coder, &output), EVX_SUCCESS);
    ASSERT_EQ(coder.finish_encode(&output), EVX_SUCCESS);

    // Decode
    int16 result[64] = {};

    entropy_coder decoder;
    decoder.clear();
    feed_stream.empty();

    ASSERT_EQ(decoder.start_decode(&output), EVX_SUCCESS);
    ASSERT_EQ(entropy_rle_stream_decode_8x8(&output, &decoder, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], input[i])
            << "RLE 8x8 roundtrip mismatch at index " << i;
    }
}

TEST(EntropyStream, RLE8x8AllZeros) {
    int16 input[64] = {};

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder coder;
    coder.clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_encode_8x8(input, &feed_stream, &coder, &output), EVX_SUCCESS);
    ASSERT_EQ(coder.finish_encode(&output), EVX_SUCCESS);

    int16 result[64] = {};

    entropy_coder decoder;
    decoder.clear();
    feed_stream.empty();

    ASSERT_EQ(decoder.start_decode(&output), EVX_SUCCESS);
    ASSERT_EQ(entropy_rle_stream_decode_8x8(&output, &decoder, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], 0)
            << "RLE 8x8 all-zeros mismatch at index " << i;
    }
}

// ---------------------------------------------------------------------------
// RLE 8x8 with dense coefficients (high frequency content)
// ---------------------------------------------------------------------------

TEST(EntropyStream, RLE8x8Dense) {
    int16 input[64] = {};
    // Fill most positions with non-zero values
    for (int i = 0; i < 48; i++) {
        input[i] = (int16)((i % 7) - 3); // values from -3 to 3
    }
    input[0] = 200; // strong DC

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder coder;
    coder.clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_encode_8x8(input, &feed_stream, &coder, &output), EVX_SUCCESS);
    ASSERT_EQ(coder.finish_encode(&output), EVX_SUCCESS);

    int16 result[64] = {};

    entropy_coder decoder;
    decoder.clear();
    feed_stream.empty();

    ASSERT_EQ(decoder.start_decode(&output), EVX_SUCCESS);
    ASSERT_EQ(entropy_rle_stream_decode_8x8(&output, &decoder, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], input[i])
            << "RLE 8x8 dense mismatch at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Context-switched RLE 8x8 encode/decode roundtrip
// ---------------------------------------------------------------------------

TEST(EntropyStream, RLE8x8CtxRoundtrip) {
    int16 input[64] = {};
    input[0] = 120;   // DC coefficient (context 0)
    input[1] = -15;   // low-AC (context 1)
    input[2] = 7;     // low-AC (context 1)
    input[8] = -3;    // low-AC (context 1)
    input[9] = 2;     // low-AC (context 1)
    input[16] = 1;    // mid-AC (context 2)

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    // Encode with single coder engine + 4 contexts
    entropy_coder enc_coder;
    entropy_context enc_contexts[4];
    enc_coder.clear();
    for (int c = 0; c < 4; c++) enc_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_encode_8x8_ctx(input, &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    // Decode with single coder engine + 4 contexts
    int16 result[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[4];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < 4; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_decode_8x8_ctx(&output, &dec_coder, dec_contexts, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], input[i])
            << "Context-switched RLE 8x8 roundtrip mismatch at index " << i;
    }
}

TEST(EntropyStream, RLE8x8CtxAllZeros) {
    int16 input[64] = {};

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[4];
    enc_coder.clear();
    for (int c = 0; c < 4; c++) enc_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_encode_8x8_ctx(input, &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    int16 result[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[4];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < 4; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_decode_8x8_ctx(&output, &dec_coder, dec_contexts, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], 0)
            << "Context-switched RLE 8x8 all-zeros mismatch at index " << i;
    }
}

TEST(EntropyStream, RLE8x8CtxVScanRoundtrip) {
    // Test VSCAN scan table roundtrip with multiple blocks through one ABAC engine
    int16 block1[64] = {};
    block1[0] = 100;  block1[1] = -10;  block1[8] = 5;  block1[9] = -2;
    block1[16] = 1;   block1[17] = -1;

    int16 block2[64] = {};
    block2[0] = 80;   block2[1] = 8;    block2[8] = -4;  block2[9] = 3;
    block2[2] = -1;   block2[10] = 1;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[4];
    enc_coder.clear();
    for (int c = 0; c < 4; c++) enc_contexts[c].clear();

    // Encode both blocks with VSCAN through same ABAC engine
    ASSERT_EQ(entropy_rle_stream_encode_8x8_ctx(block1, EVX_MACROBLOCK_8x8_VSCAN,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    ASSERT_EQ(entropy_rle_stream_encode_8x8_ctx(block2, EVX_MACROBLOCK_8x8_VSCAN,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    // Decode both blocks
    int16 result1[64] = {}, result2[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[4];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < 4; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_VSCAN,
              &dec_coder, dec_contexts, &feed_stream, result1), EVX_SUCCESS);
    ASSERT_EQ(entropy_rle_stream_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_VSCAN,
              &dec_coder, dec_contexts, &feed_stream, result2), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result1[i], block1[i])
            << "VSCAN block 1 mismatch at spatial index " << i;
    }
    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result2[i], block2[i])
            << "VSCAN block 2 mismatch at spatial index " << i;
    }
}

TEST(EntropyStream, RLE8x8CtxHScanRoundtrip) {
    // Test HSCAN scan table roundtrip
    int16 block[64] = {};
    block[0] = 100;  block[1] = -10;  block[8] = 5;  block[9] = -2;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[4];
    enc_coder.clear();
    for (int c = 0; c < 4; c++) enc_contexts[c].clear();

    ASSERT_EQ(entropy_rle_stream_encode_8x8_ctx(block, EVX_MACROBLOCK_8x8_HSCAN,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    int16 result[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[4];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < 4; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_rle_stream_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_HSCAN,
              &dec_coder, dec_contexts, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], block[i])
            << "HSCAN roundtrip mismatch at spatial index " << i;
    }
}

// ---------------------------------------------------------------------------
// Split entropy contexts test
// ---------------------------------------------------------------------------

#if EVX_ENABLE_SIGMAP_CODING

TEST(EntropyStream, SplitContextsProduceDifferentProbabilities) {
    // With sigmap coding, use EVX_SIGMAP_CONTEXTS_PER_SET offset between intra/inter.
    int16 intra_block[64] = {};
    intra_block[0] = 500;
    intra_block[1] = -80;
    intra_block[2] = 40;
    intra_block[8] = -30;
    intra_block[9] = 20;
    intra_block[16] = 10;

    int16 inter_block[64] = {};
    inter_block[0] = 5;
    inter_block[1] = -2;
    inter_block[2] = 1;
    inter_block[8] = -1;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(256 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[EVX_SIGMAP_CONTEXTS_PER_SET * 2];
    enc_coder.clear();
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET * 2; c++) enc_contexts[c].clear();

    // Encode multiple intra blocks through contexts 0..87
    for (int b = 0; b < 10; b++) {
        ASSERT_EQ(entropy_sigmap_encode_8x8_ctx(intra_block, EVX_MACROBLOCK_8x8_ZIGZAG,
                  &feed_stream, &enc_coder, enc_contexts + 0, &output), EVX_SUCCESS);
    }

    // Encode multiple inter blocks through contexts 88..175
    for (int b = 0; b < 10; b++) {
        ASSERT_EQ(entropy_sigmap_encode_8x8_ctx(inter_block, EVX_MACROBLOCK_8x8_ZIGZAG,
                  &feed_stream, &enc_coder, enc_contexts + EVX_SIGMAP_CONTEXTS_PER_SET, &output), EVX_SUCCESS);
    }

    enc_coder.finish_encode(&output);

    // Verify that intra and inter contexts have diverged.
    bool any_different = false;
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) {
        if (enc_contexts[c].history[0] != enc_contexts[c + EVX_SIGMAP_CONTEXTS_PER_SET].history[0] ||
            enc_contexts[c].history[1] != enc_contexts[c + EVX_SIGMAP_CONTEXTS_PER_SET].history[1]) {
            any_different = true;
            break;
        }
    }

    EXPECT_TRUE(any_different)
        << "Intra and inter contexts should diverge after encoding different distributions";
}

#elif EVX_ENABLE_SPLIT_ENTROPY_CONTEXTS

TEST(EntropyStream, SplitContextsProduceDifferentProbabilities) {
    int16 intra_block[64] = {};
    intra_block[0] = 500;
    intra_block[1] = -80;
    intra_block[2] = 40;
    intra_block[8] = -30;
    intra_block[9] = 20;
    intra_block[16] = 10;

    int16 inter_block[64] = {};
    inter_block[0] = 5;
    inter_block[1] = -2;
    inter_block[2] = 1;
    inter_block[8] = -1;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[8];
    enc_coder.clear();
    for (int c = 0; c < 8; c++) enc_contexts[c].clear();

    for (int b = 0; b < 10; b++) {
        ASSERT_EQ(entropy_rle_stream_encode_8x8_ctx(intra_block, &feed_stream,
                  &enc_coder, enc_contexts + 0, &output), EVX_SUCCESS);
    }

    for (int b = 0; b < 10; b++) {
        ASSERT_EQ(entropy_rle_stream_encode_8x8_ctx(inter_block, &feed_stream,
                  &enc_coder, enc_contexts + 4, &output), EVX_SUCCESS);
    }

    enc_coder.finish_encode(&output);

    bool any_different = false;
    for (int c = 0; c < 4; c++) {
        if (enc_contexts[c].history[0] != enc_contexts[c + 4].history[0] ||
            enc_contexts[c].history[1] != enc_contexts[c + 4].history[1]) {
            any_different = true;
            break;
        }
    }

    EXPECT_TRUE(any_different)
        << "Intra and inter contexts should diverge after encoding different distributions";
}

#endif

// ---------------------------------------------------------------------------
// Sigmap coefficient coding roundtrip tests
// ---------------------------------------------------------------------------

#if EVX_ENABLE_SIGMAP_CODING

TEST(SigmapStream, SparseRoundtrip) {
    int16 input[64] = {};
    input[0] = 120;   // DC
    input[1] = -15;
    input[2] = 7;
    input[8] = -3;
    input[9] = 2;
    input[16] = 1;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    enc_coder.clear();
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) enc_contexts[c].clear();

    ASSERT_EQ(entropy_sigmap_encode_8x8_ctx(input, EVX_MACROBLOCK_8x8_ZIGZAG,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    int16 result[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_sigmap_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_ZIGZAG,
              &dec_coder, dec_contexts, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], input[i])
            << "Sigmap sparse roundtrip mismatch at index " << i;
    }
}

TEST(SigmapStream, AllZeros) {
    int16 input[64] = {};

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    enc_coder.clear();
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) enc_contexts[c].clear();

    ASSERT_EQ(entropy_sigmap_encode_8x8_ctx(input, EVX_MACROBLOCK_8x8_ZIGZAG,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    int16 result[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_sigmap_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_ZIGZAG,
              &dec_coder, dec_contexts, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], 0) << "Sigmap all-zeros mismatch at index " << i;
    }
}

TEST(SigmapStream, DenseRoundtrip) {
    int16 input[64] = {};
    for (int i = 0; i < 48; i++) {
        input[i] = (int16)((i % 7) - 3);
    }
    input[0] = 200;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    enc_coder.clear();
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) enc_contexts[c].clear();

    ASSERT_EQ(entropy_sigmap_encode_8x8_ctx(input, EVX_MACROBLOCK_8x8_ZIGZAG,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    int16 result[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_sigmap_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_ZIGZAG,
              &dec_coder, dec_contexts, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], input[i])
            << "Sigmap dense roundtrip mismatch at index " << i;
    }
}

TEST(SigmapStream, LargeCoefficients) {
    int16 input[64] = {};
    input[0] = 500;
    input[1] = -300;
    input[2] = 100;
    input[8] = -50;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    enc_coder.clear();
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) enc_contexts[c].clear();

    ASSERT_EQ(entropy_sigmap_encode_8x8_ctx(input, EVX_MACROBLOCK_8x8_ZIGZAG,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    int16 result[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_sigmap_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_ZIGZAG,
              &dec_coder, dec_contexts, &feed_stream, result), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result[i], input[i])
            << "Sigmap large coeff roundtrip mismatch at index " << i;
    }
}

TEST(SigmapStream, MultiBlockRoundtrip) {
    int16 block1[64] = {};
    block1[0] = 100;  block1[1] = -10;  block1[8] = 5;

    int16 block2[64] = {};
    block2[0] = 80;   block2[1] = 8;    block2[8] = -4;

    bit_stream feed_stream;
    feed_stream.resize_capacity(64 * 1024);
    bit_stream output;
    output.resize_capacity(64 * 1024);

    entropy_coder enc_coder;
    entropy_context enc_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    enc_coder.clear();
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) enc_contexts[c].clear();

    ASSERT_EQ(entropy_sigmap_encode_8x8_ctx(block1, EVX_MACROBLOCK_8x8_ZIGZAG,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    ASSERT_EQ(entropy_sigmap_encode_8x8_ctx(block2, EVX_MACROBLOCK_8x8_ZIGZAG,
              &feed_stream, &enc_coder, enc_contexts, &output), EVX_SUCCESS);
    enc_coder.finish_encode(&output);

    int16 result1[64] = {}, result2[64] = {};
    entropy_coder dec_coder;
    entropy_context dec_contexts[EVX_SIGMAP_CONTEXTS_PER_SET];
    dec_coder.clear();
    dec_coder.start_decode(&output);
    for (int c = 0; c < EVX_SIGMAP_CONTEXTS_PER_SET; c++) dec_contexts[c].clear();
    feed_stream.empty();

    ASSERT_EQ(entropy_sigmap_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_ZIGZAG,
              &dec_coder, dec_contexts, &feed_stream, result1), EVX_SUCCESS);
    ASSERT_EQ(entropy_sigmap_decode_8x8_ctx(&output, EVX_MACROBLOCK_8x8_ZIGZAG,
              &dec_coder, dec_contexts, &feed_stream, result2), EVX_SUCCESS);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result1[i], block1[i]) << "Sigmap multi-block block1 mismatch at " << i;
    }
    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(result2[i], block2[i]) << "Sigmap multi-block block2 mismatch at " << i;
    }
}

#endif // EVX_ENABLE_SIGMAP_CODING
