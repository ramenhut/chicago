#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include "audio/config.h"
#include "audio/transform.h"

using namespace evx;

static const float PI = 3.14159265358979323846f;

TEST(AudioTransform, SineWindowOrthogonality) {
    float32 window[EVX_AUDIO_WINDOW_SIZE];
    audio_generate_sine_window(window);

    // Princen-Bradley condition: w[n]^2 + w[n+N/2]^2 = 1
    for (int n = 0; n < EVX_AUDIO_HOP_SIZE; n++) {
        float32 sum = window[n] * window[n] +
                      window[n + EVX_AUDIO_HOP_SIZE] * window[n + EVX_AUDIO_HOP_SIZE];
        EXPECT_NEAR(sum, 1.0f, 1e-6f) << "Princen-Bradley failed at n=" << n;
    }
}

TEST(AudioTransform, SineWindowSymmetry) {
    float32 window[EVX_AUDIO_WINDOW_SIZE];
    audio_generate_sine_window(window);

    // Sine window should be symmetric: w[n] = w[N-1-n]
    for (int n = 0; n < EVX_AUDIO_WINDOW_SIZE / 2; n++) {
        EXPECT_NEAR(window[n], window[EVX_AUDIO_WINDOW_SIZE - 1 - n], 1e-6f);
    }
}

TEST(AudioTransform, MdctImdctRoundtrip) {
    float32 window[EVX_AUDIO_WINDOW_SIZE];
    audio_generate_sine_window(window);

    // Create a test signal: two overlapping frames.
    float32 signal[EVX_AUDIO_WINDOW_SIZE + EVX_AUDIO_HOP_SIZE];
    for (int n = 0; n < EVX_AUDIO_WINDOW_SIZE + EVX_AUDIO_HOP_SIZE; n++) {
        signal[n] = sinf(2.0f * PI * 440.0f * n / 44100.0f);
    }

    float32 mdct_out[EVX_AUDIO_FREQ_COEFFS];
    float32 imdct_out[EVX_AUDIO_WINDOW_SIZE];
    float32 overlap_prev[EVX_AUDIO_HOP_SIZE];
    float32 pcm_out[EVX_AUDIO_HOP_SIZE];

    memset(overlap_prev, 0, sizeof(overlap_prev));

    // Frame 0: process samples 0..1023.
    float32 frame0[EVX_AUDIO_WINDOW_SIZE];
    memcpy(frame0, signal, sizeof(float32) * EVX_AUDIO_WINDOW_SIZE);
    audio_apply_window(frame0, window);
    audio_mdct_forward(frame0, mdct_out);
    audio_mdct_inverse(mdct_out, imdct_out);
    audio_overlap_add(imdct_out, window, pcm_out, overlap_prev);
    // Frame 0 output is priming — skip it.

    // Frame 1: process samples 512..1535.
    float32 frame1[EVX_AUDIO_WINDOW_SIZE];
    memcpy(frame1, signal + EVX_AUDIO_HOP_SIZE, sizeof(float32) * EVX_AUDIO_WINDOW_SIZE);
    audio_apply_window(frame1, window);
    audio_mdct_forward(frame1, mdct_out);
    audio_mdct_inverse(mdct_out, imdct_out);
    audio_overlap_add(imdct_out, window, pcm_out, overlap_prev);

    // Frame 1's output should match signal[512..1023] with high accuracy.
    for (int n = 0; n < EVX_AUDIO_HOP_SIZE; n++) {
        EXPECT_NEAR(pcm_out[n], signal[EVX_AUDIO_HOP_SIZE + n], 5e-4f)
            << "Roundtrip error at sample " << n;
    }
}

TEST(AudioTransform, DcOnlySignal) {
    float32 window[EVX_AUDIO_WINDOW_SIZE];
    audio_generate_sine_window(window);

    // DC-only signal.
    float32 frame[EVX_AUDIO_WINDOW_SIZE];
    for (int n = 0; n < EVX_AUDIO_WINDOW_SIZE; n++)
        frame[n] = 1.0f;

    audio_apply_window(frame, window);

    float32 mdct_out[EVX_AUDIO_FREQ_COEFFS];
    audio_mdct_forward(frame, mdct_out);

    // Most energy should be in the first few coefficients.
    float32 low_energy = 0, high_energy = 0;
    for (int k = 0; k < 4; k++)
        low_energy += mdct_out[k] * mdct_out[k];
    for (int k = 4; k < EVX_AUDIO_FREQ_COEFFS; k++)
        high_energy += mdct_out[k] * mdct_out[k];

    EXPECT_GT(low_energy, high_energy * 10.0f)
        << "DC signal should concentrate energy in low coefficients";
}

TEST(AudioTransform, OverlapStateClears) {
    audio_overlap_state state;
    state.prev_samples[0][0] = 42.0f;
    state.clear();

    for (int ch = 0; ch < EVX_AUDIO_MAX_CHANNELS; ch++)
    for (int n = 0; n < EVX_AUDIO_HOP_SIZE; n++) {
        EXPECT_EQ(state.prev_samples[ch][n], 0.0f);
    }
}
