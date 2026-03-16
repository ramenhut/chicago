
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// audio_transform.h
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

#ifndef __EVX_AUDIO_TRANSFORM_H__
#define __EVX_AUDIO_TRANSFORM_H__

#include "base.h"
#include "config.h"

namespace evx {

// Generates a sine window of length EVX_AUDIO_WINDOW_SIZE satisfying the
// Princen-Bradley condition: w[n]^2 + w[n+N/2]^2 = 1.
void audio_generate_sine_window(float32 *window_out);

// Forward MDCT: transforms EVX_AUDIO_WINDOW_SIZE windowed samples into
// EVX_AUDIO_FREQ_COEFFS spectral coefficients.
void audio_mdct_forward(const float32 *windowed_input, float32 *output);

// Inverse MDCT: transforms EVX_AUDIO_FREQ_COEFFS spectral coefficients
// back to EVX_AUDIO_WINDOW_SIZE time-domain samples.
void audio_mdct_inverse(const float32 *input, float32 *output);

// Applies the analysis/synthesis window in-place to EVX_AUDIO_WINDOW_SIZE samples.
void audio_apply_window(float32 *samples, const float32 *window);

// Overlap-add: combines the current IMDCT output (re-windowed) with the
// previous frame's tail to produce EVX_AUDIO_HOP_SIZE output PCM samples.
// Updates overlap_prev with the current frame's tail for the next call.
void audio_overlap_add(const float32 *imdct_output, const float32 *window,
                       float32 *pcm_output, float32 *overlap_prev);

// Per-channel overlap-add state for the decoder.
struct audio_overlap_state
{
    float32 prev_samples[EVX_AUDIO_MAX_CHANNELS][EVX_AUDIO_HOP_SIZE];

    void clear();
};

} // namespace evx

#endif // __EVX_AUDIO_TRANSFORM_H__
