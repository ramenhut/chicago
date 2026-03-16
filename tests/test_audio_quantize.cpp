#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include "audio/config.h"
#include "audio/quantize.h"

using namespace evx;

TEST(AudioQuantize, BarkBandCoverage) {
    audio_bark_bands bands;
    audio_init_bark_bands(44100, &bands);

    // First band should start at 0.
    EXPECT_EQ(bands.start[0], 0);

    // Last band should end at EVX_AUDIO_FREQ_COEFFS.
    EXPECT_EQ(bands.end[EVX_AUDIO_BARK_BAND_COUNT - 1], EVX_AUDIO_FREQ_COEFFS);

    // Bands should be contiguous (no gaps).
    for (int b = 1; b < EVX_AUDIO_BARK_BAND_COUNT; b++) {
        EXPECT_EQ(bands.start[b], bands.end[b - 1])
            << "Gap between bands " << (b - 1) << " and " << b;
    }

    // Each band should have non-zero width.
    for (int b = 0; b < EVX_AUDIO_BARK_BAND_COUNT; b++) {
        EXPECT_GT(bands.end[b], bands.start[b])
            << "Band " << b << " has zero width";
    }
}

TEST(AudioQuantize, QuantMatrixShape) {
    audio_bark_bands bands;
    audio_init_bark_bands(44100, &bands);

    float32 qm[EVX_AUDIO_FREQ_COEFFS];
    audio_generate_quant_matrix(bands, qm);

    // QM should be monotonically non-decreasing (low freq -> high freq).
    // Check that overall trend is increasing.
    EXPECT_LT(qm[0], qm[EVX_AUDIO_FREQ_COEFFS - 1])
        << "QM should increase from low to high frequency";

    // DC should have the smallest QM value (~1.0).
    EXPECT_NEAR(qm[0], 1.0f, 0.5f);

    // High frequency should have a larger QM value.
    EXPECT_GT(qm[EVX_AUDIO_FREQ_COEFFS - 1], 4.0f);
}

TEST(AudioQuantize, QuantizeRoundtrip) {
    audio_bark_bands bands;
    audio_init_bark_bands(44100, &bands);

    float32 qm[EVX_AUDIO_FREQ_COEFFS];
    audio_generate_quant_matrix(bands, qm);

    float32 coeffs[EVX_AUDIO_FREQ_COEFFS];
    for (int k = 0; k < EVX_AUDIO_FREQ_COEFFS; k++)
        coeffs[k] = sinf(k * 0.1f) * 100.0f;

    // At quality=0 (finest), roundtrip should be close.
    float32 step = audio_quality_to_step(0);
    int16 quantized[EVX_AUDIO_FREQ_COEFFS];
    float32 dequantized[EVX_AUDIO_FREQ_COEFFS];

    audio_quantize(coeffs, qm, step, quantized);
    audio_dequantize(quantized, qm, step, dequantized);

    float32 mse = 0;
    for (int k = 0; k < EVX_AUDIO_FREQ_COEFFS; k++) {
        float32 err = coeffs[k] - dequantized[k];
        mse += err * err;
    }
    mse /= EVX_AUDIO_FREQ_COEFFS;

    // At quality=0, MSE should be small.
    EXPECT_LT(mse, 1.0f) << "Quantize/dequantize MSE at quality=0 should be small";
}

TEST(AudioQuantize, QualityLadder) {
    // Higher quality numbers should produce larger step sizes.
    float32 prev_step = audio_quality_to_step(0);
    for (uint8 q = 1; q <= EVX_AUDIO_MAX_QUALITY; q++) {
        float32 step = audio_quality_to_step(q);
        EXPECT_GT(step, prev_step)
            << "Step should increase with quality parameter at q=" << (int)q;
        prev_step = step;
    }
}

TEST(AudioQuantize, ZeroCoeffsQuantizeToZero) {
    audio_bark_bands bands;
    audio_init_bark_bands(44100, &bands);

    float32 qm[EVX_AUDIO_FREQ_COEFFS];
    audio_generate_quant_matrix(bands, qm);

    float32 zero_coeffs[EVX_AUDIO_FREQ_COEFFS];
    memset(zero_coeffs, 0, sizeof(zero_coeffs));

    int16 quantized[EVX_AUDIO_FREQ_COEFFS];
    audio_quantize(zero_coeffs, qm, audio_quality_to_step(8), quantized);

    for (int k = 0; k < EVX_AUDIO_FREQ_COEFFS; k++) {
        EXPECT_EQ(quantized[k], 0);
    }
}
