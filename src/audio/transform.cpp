
#include "transform.h"
#include <cmath>
#include <cstring>

namespace evx {

static const float32 EVX_PI = 3.14159265358979323846f;

void audio_generate_sine_window(float32 *window_out)
{
    for (int32 n = 0; n < EVX_AUDIO_WINDOW_SIZE; n++)
    {
        window_out[n] = sinf(EVX_PI * (n + 0.5f) / EVX_AUDIO_WINDOW_SIZE);
    }
}

void audio_mdct_forward(const float32 *windowed_input, float32 *output)
{
    const int32 N = EVX_AUDIO_WINDOW_SIZE;
    const int32 K = EVX_AUDIO_FREQ_COEFFS;
    const float32 scale = EVX_PI / N;

    for (int32 k = 0; k < K; k++)
    {
        float32 sum = 0.0f;

        for (int32 n = 0; n < N; n++)
        {
            sum += windowed_input[n] * cosf(scale * (n + 0.5f + N * 0.25f) * (2 * k + 1));
        }

        output[k] = sum;
    }
}

void audio_mdct_inverse(const float32 *input, float32 *output)
{
    const int32 N = EVX_AUDIO_WINDOW_SIZE;
    const int32 K = EVX_AUDIO_FREQ_COEFFS;
    const float32 scale = EVX_PI / N;
    const float32 norm = 4.0f / N;

    for (int32 n = 0; n < N; n++)
    {
        float32 sum = 0.0f;

        for (int32 k = 0; k < K; k++)
        {
            sum += input[k] * cosf(scale * (n + 0.5f + N * 0.25f) * (2 * k + 1));
        }

        output[n] = sum * norm;
    }
}

void audio_apply_window(float32 *samples, const float32 *window)
{
    for (int32 n = 0; n < EVX_AUDIO_WINDOW_SIZE; n++)
    {
        samples[n] *= window[n];
    }
}

void audio_overlap_add(const float32 *imdct_output, const float32 *window,
                       float32 *pcm_output, float32 *overlap_prev)
{
    const int32 hop = EVX_AUDIO_HOP_SIZE;

    // Re-window the IMDCT output, then overlap-add with previous frame's tail.
    for (int32 n = 0; n < hop; n++)
    {
        float32 windowed = imdct_output[n] * window[n];
        pcm_output[n] = windowed + overlap_prev[n];
    }

    // Store the second half (re-windowed) as the new overlap buffer.
    for (int32 n = 0; n < hop; n++)
    {
        overlap_prev[n] = imdct_output[hop + n] * window[hop + n];
    }
}

void audio_overlap_state::clear()
{
    memset(prev_samples, 0, sizeof(prev_samples));
}

} // namespace evx
