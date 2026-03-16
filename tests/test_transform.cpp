#include <gtest/gtest.h>
#include "video/transform.h"

#include <cmath>
#include <cstring>

using namespace evx;

// ---------------------------------------------------------------------------
// Helper: fill an NxN block with a ramp of pixel-like values starting at
// |base|, incrementing by 1 across rows then columns.
// ---------------------------------------------------------------------------
static void fill_ramp(int16 *block, uint32 n, int16 base) {
    for (uint32 i = 0; i < n * n; i++) {
        block[i] = base + static_cast<int16>(i);
    }
}

// ---------------------------------------------------------------------------
// Helper: check that every element of |a| and |b| (NxN) differ by at most
// |tolerance|.
// ---------------------------------------------------------------------------
static void expect_near(const int16 *a, const int16 *b, uint32 n, int tolerance,
                        const char *context) {
    for (uint32 r = 0; r < n; r++) {
        for (uint32 c = 0; c < n; c++) {
            uint32 idx = r * n + c;
            EXPECT_NEAR(a[idx], b[idx], tolerance)
                << context << " mismatch at [" << r << "," << c << "]";
        }
    }
}

// ---------------------------------------------------------------------------
// Helper: sum of squares for an NxN block.
// ---------------------------------------------------------------------------
static double sum_of_squares(const int16 *block, uint32 n) {
    double acc = 0.0;
    for (uint32 i = 0; i < n * n; i++) {
        acc += static_cast<double>(block[i]) * block[i];
    }
    return acc;
}

// ===========================================================================
// 1. Forward / inverse roundtrip - 4x4
// ===========================================================================
TEST(Transform, Roundtrip4x4) {
    int16 src[4 * 4];
    int16 freq[4 * 4];
    int16 recon[4 * 4];

    fill_ramp(src, 4, 10); // values 10..25

    memset(freq, 0, sizeof(freq));
    memset(recon, 0, sizeof(recon));

    transform_4x4(src, 4, freq, 4);
    inverse_transform_4x4(freq, 4, recon, 4);

    expect_near(src, recon, 4, 1, "Roundtrip4x4");
}

// ===========================================================================
// 2. Forward / inverse roundtrip - 8x8
//
//    The 8x8 integer DCT accumulates more truncation error than the 4x4
//    variant due to the larger number of terms (8 vs 4) and the two-pass
//    (horizontal + vertical) fixed-point scaling.  Observed max error is 2.
// ===========================================================================
TEST(Transform, Roundtrip8x8) {
    int16 src[8 * 8];
    int16 freq[8 * 8];
    int16 recon[8 * 8];

    fill_ramp(src, 8, 10); // values 10..73

    memset(freq, 0, sizeof(freq));
    memset(recon, 0, sizeof(recon));

    transform_8x8(src, 8, freq, 8);
    inverse_transform_8x8(freq, 8, recon, 8);

    expect_near(src, recon, 8, 2, "Roundtrip8x8");
}

// ===========================================================================
// 3. Forward / inverse roundtrip - 16x16
//
//    The 16x16 transform is implemented as four independent 8x8 transforms
//    on sub-blocks.  The ramp here spans values 10..265, so the bottom-right
//    sub-block processes significantly larger magnitudes than the 8x8 test.
//    Larger inputs amplify fixed-point truncation errors; observed max error
//    is 6.  We use tolerance 7 for a small safety margin.
// ===========================================================================
TEST(Transform, Roundtrip16x16) {
    int16 src[16 * 16];
    int16 freq[16 * 16];
    int16 recon[16 * 16];

    fill_ramp(src, 16, 10); // values 10..265

    memset(freq, 0, sizeof(freq));
    memset(recon, 0, sizeof(recon));

    transform_16x16(src, 16, freq, 16);
    inverse_transform_16x16(freq, 16, recon, 16);

    expect_near(src, recon, 16, 7, "Roundtrip16x16");
}

// ===========================================================================
// 4. DC-only input: constant block -> energy concentrated in [0,0]
// ===========================================================================
TEST(Transform, DCOnlyInput8x8) {
    const int16 kDCValue = 100;
    int16 src[8 * 8];
    int16 freq[8 * 8];

    for (int i = 0; i < 64; i++) {
        src[i] = kDCValue;
    }
    memset(freq, 0, sizeof(freq));

    transform_8x8(src, 8, freq, 8);

    // The DC coefficient should hold the vast majority of the energy.
    double dc_energy = static_cast<double>(freq[0]) * freq[0];
    double total_energy = sum_of_squares(freq, 8);

    ASSERT_GT(total_energy, 0.0);
    double ratio = dc_energy / total_energy;
    EXPECT_GT(ratio, 0.99)
        << "DC coefficient should contain >99% of energy for constant input, got "
        << ratio * 100.0 << "%";
}

// ===========================================================================
// 5. sub_transform + inverse_transform_add roundtrip - 8x8
//
//    sub_transform_8x8 computes DCT(src - sub).
//    inverse_transform_add_8x8 computes IDCT(freq) + add.
//    So: IDCT(DCT(src - sub)) + sub  should  ~=  src.
// ===========================================================================
TEST(Transform, SubTransformAddRoundtrip8x8) {
    int16 src[8 * 8];
    int16 sub[8 * 8];
    int16 freq[8 * 8];
    int16 recon[8 * 8];

    // Source: ramp starting at 50
    fill_ramp(src, 8, 50);

    // Subtraction block: ramp starting at 10 (simulates a prediction)
    fill_ramp(sub, 8, 10);

    memset(freq, 0, sizeof(freq));
    memset(recon, 0, sizeof(recon));

    sub_transform_8x8(src, 8, sub, 8, freq, 8);
    inverse_transform_add_8x8(freq, 8, sub, 8, recon, 8);

    expect_near(src, recon, 8, 1, "SubTransformAddRoundtrip8x8");
}

// ===========================================================================
// 6. Zero input produces zero output
// ===========================================================================
TEST(Transform, ZeroInput4x4) {
    int16 src[4 * 4] = {};
    int16 freq[4 * 4];

    // Fill freq with garbage to ensure the transform really writes zeros.
    memset(freq, 0x7F, sizeof(freq));

    transform_4x4(src, 4, freq, 4);

    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(freq[i], 0) << "Expected zero at index " << i;
    }
}

TEST(Transform, ZeroInput8x8) {
    int16 src[8 * 8] = {};
    int16 freq[8 * 8];

    memset(freq, 0x7F, sizeof(freq));

    transform_8x8(src, 8, freq, 8);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(freq[i], 0) << "Expected zero at index " << i;
    }
}

TEST(Transform, ZeroInput16x16) {
    int16 src[16 * 16] = {};
    int16 freq[16 * 16];

    memset(freq, 0x7F, sizeof(freq));

    transform_16x16(src, 16, freq, 16);

    for (int i = 0; i < 256; i++) {
        EXPECT_EQ(freq[i], 0) << "Expected zero at index " << i;
    }
}

// ===========================================================================
// 7. Energy preservation (Parseval-like property)
//
//    For an orthonormal DCT the sum of squares is preserved exactly.
//    Integer implementations may not be perfectly orthonormal, so we check
//    that the ratio of energies is within a reasonable range.
// ===========================================================================
TEST(Transform, EnergyPreservation8x8) {
    int16 src[8 * 8];
    int16 freq[8 * 8];

    fill_ramp(src, 8, 1);
    memset(freq, 0, sizeof(freq));

    transform_8x8(src, 8, freq, 8);

    double src_energy = sum_of_squares(src, 8);
    double freq_energy = sum_of_squares(freq, 8);

    ASSERT_GT(src_energy, 0.0);
    ASSERT_GT(freq_energy, 0.0);

    // The ratio should be roughly constant for a given block size. Rather
    // than demanding an exact value (which depends on the DCT's scaling
    // convention), we verify the ratio is within a generous but finite band:
    // between 0.01 and 100. This catches catastrophic under/overflow bugs
    // while remaining tolerant of different normalization choices.
    double ratio = freq_energy / src_energy;
    EXPECT_GT(ratio, 0.01)
        << "Transform energy implausibly small (ratio=" << ratio << ")";
    EXPECT_LT(ratio, 100.0)
        << "Transform energy implausibly large (ratio=" << ratio << ")";
}

// Also verify for 4x4 to exercise a different block size path.
TEST(Transform, EnergyPreservation4x4) {
    int16 src[4 * 4];
    int16 freq[4 * 4];

    fill_ramp(src, 4, 1);
    memset(freq, 0, sizeof(freq));

    transform_4x4(src, 4, freq, 4);

    double src_energy = sum_of_squares(src, 4);
    double freq_energy = sum_of_squares(freq, 4);

    ASSERT_GT(src_energy, 0.0);
    ASSERT_GT(freq_energy, 0.0);

    double ratio = freq_energy / src_energy;
    EXPECT_GT(ratio, 0.01)
        << "Transform energy implausibly small (ratio=" << ratio << ")";
    EXPECT_LT(ratio, 100.0)
        << "Transform energy implausibly large (ratio=" << ratio << ")";
}
