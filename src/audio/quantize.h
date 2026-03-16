
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// audio_quantize.h
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Additional Information:
//
//   For more information, visit http://www.bertolami.com.
*/

#ifndef __EVX_AUDIO_QUANTIZE_H__
#define __EVX_AUDIO_QUANTIZE_H__

#include "base.h"
#include "config.h"

namespace evx {

// Bark-scale critical band boundaries for MDCT frequency bins.
struct audio_bark_bands
{
    uint16 start[EVX_AUDIO_BARK_BAND_COUNT];
    uint16 end[EVX_AUDIO_BARK_BAND_COUNT];
};

// Initialize bark band boundaries for a given sample rate.
void audio_init_bark_bands(uint32 sample_rate, audio_bark_bands *bands);

// Generate a quantization matrix (512 values) shaped by bark-scale frequency.
// Lower frequencies get finer quantization, higher frequencies get coarser.
void audio_generate_quant_matrix(const audio_bark_bands &bands, float32 *qm_out);

// Convert quality parameter (0-31) to a quantization step size.
// step = 0.5 * 2^(quality/4)
float32 audio_quality_to_step(uint8 quality);

// Forward quantization: divides MDCT coefficients by qm-scaled step.
void audio_quantize(const float32 *coeffs, const float32 *qm, float32 step,
                    int16 *output);

// Inverse quantization: multiplies quantized levels by qm-scaled step.
void audio_dequantize(const int16 *levels, const float32 *qm, float32 step,
                      float32 *output);

} // namespace evx

#endif // __EVX_AUDIO_QUANTIZE_H__
