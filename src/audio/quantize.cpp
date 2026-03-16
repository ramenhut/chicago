
#include "quantize.h"
#include <cmath>
#include <cstring>

namespace evx {

// Bark frequency boundaries (Hz) for 25 critical bands.
// Approximation of the Zwicker bark scale up to ~15.5 kHz.
static const float32 bark_edges_hz[EVX_AUDIO_BARK_BAND_COUNT + 1] = {
      0,   100,   200,   300,   400,   510,   630,   770,
    920,  1080,  1270,  1480,  1720,  2000,  2320,  2700,
   3150,  3700,  4400,  5300,  6400,  7700,  9500, 12000,
  15500, 24000
};

void audio_init_bark_bands(uint32 sample_rate, audio_bark_bands *bands)
{
    float32 nyquist = sample_rate * 0.5f;
    float32 bin_width = nyquist / EVX_AUDIO_FREQ_COEFFS;

    memset(bands, 0, sizeof(audio_bark_bands));

    for (int32 b = 0; b < EVX_AUDIO_BARK_BAND_COUNT; b++)
    {
        float32 lo_hz = bark_edges_hz[b];
        float32 hi_hz = bark_edges_hz[b + 1];

        // Clamp to Nyquist.
        if (lo_hz >= nyquist) lo_hz = nyquist;
        if (hi_hz > nyquist) hi_hz = nyquist;

        uint16 lo_bin = (uint16)(lo_hz / bin_width);
        uint16 hi_bin = (uint16)(hi_hz / bin_width);

        if (lo_bin >= EVX_AUDIO_FREQ_COEFFS) lo_bin = EVX_AUDIO_FREQ_COEFFS - 1;
        if (hi_bin > EVX_AUDIO_FREQ_COEFFS) hi_bin = EVX_AUDIO_FREQ_COEFFS;
        if (hi_bin <= lo_bin) hi_bin = lo_bin + 1;
        if (hi_bin > EVX_AUDIO_FREQ_COEFFS) hi_bin = EVX_AUDIO_FREQ_COEFFS;

        bands->start[b] = lo_bin;
        bands->end[b] = hi_bin;
    }

    // Ensure the last band covers up to the end.
    bands->end[EVX_AUDIO_BARK_BAND_COUNT - 1] = EVX_AUDIO_FREQ_COEFFS;
}

void audio_generate_quant_matrix(const audio_bark_bands &bands, float32 *qm_out)
{
    // QM curve: lower bands get smaller values (finer quantization),
    // higher bands get larger values (coarser quantization).
    // Mild perceptual curve: slightly coarser at very high frequencies.
    // Range: 1.0 at DC to 2.0 at Nyquist.
    const float32 qm_base = 1.0f;
    const float32 qm_amplitude = 1.0f;
    const float32 qm_exponent = 2.0f;

    for (int32 b = 0; b < EVX_AUDIO_BARK_BAND_COUNT; b++)
    {
        float32 bark_ratio = (float32)b / (EVX_AUDIO_BARK_BAND_COUNT - 1);
        float32 qm_value = qm_base + qm_amplitude * powf(bark_ratio, qm_exponent);

        for (uint16 k = bands.start[b]; k < bands.end[b] && k < EVX_AUDIO_FREQ_COEFFS; k++)
        {
            qm_out[k] = qm_value;
        }
    }
}

float32 audio_quality_to_step(uint8 quality)
{
    if (quality > EVX_AUDIO_MAX_QUALITY)
        quality = EVX_AUDIO_MAX_QUALITY;

    return 0.02f * powf(2.0f, (float32)quality / 3.0f);
}

void audio_quantize(const float32 *coeffs, const float32 *qm, float32 step,
                    int16 *output)
{
    for (int32 k = 0; k < EVX_AUDIO_FREQ_COEFFS; k++)
    {
        float32 divisor = step * qm[k];
        float32 val = coeffs[k] / divisor;

        // Round to nearest integer (with sign preservation and clamping).
        int32 rounded = (val >= 0.0f) ? (int32)(val + 0.5f) : (int32)(val - 0.5f);
        if (rounded > 32767) rounded = 32767;
        if (rounded < -32768) rounded = -32768;
        output[k] = (int16)rounded;
    }
}

void audio_dequantize(const int16 *levels, const float32 *qm, float32 step,
                      float32 *output)
{
    for (int32 k = 0; k < EVX_AUDIO_FREQ_COEFFS; k++)
    {
        output[k] = (float32)levels[k] * step * qm[k];
    }
}

} // namespace evx
