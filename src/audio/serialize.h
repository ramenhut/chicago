
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// audio_serialize.h
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

#ifndef __EVX_AUDIO_SERIALIZE_H__
#define __EVX_AUDIO_SERIALIZE_H__

#include "base.h"
#include "config.h"
#include "bitstream.h"
#include "abac.h"

namespace evx {

// Entropy contexts for audio coefficient coding.
// DC gets its own context; the remaining 511 coefficients are split
// into 7 frequency bands (sub-bass through brilliance).
struct audio_entropy_contexts
{
    entropy_context dc_ctx;
    entropy_context band_ctx[EVX_AUDIO_ENTROPY_BAND_COUNT - 1];

    void clear();
};

// Serialize quantized MDCT coefficients to the output bitstream.
// Uses delta-DC coding when EVX_AUDIO_ENABLE_DELTA_DC is enabled.
evx_status audio_serialize_coefficients(const int16 *coeffs, int16 last_dc,
    bit_stream *feed_stream, entropy_coder *coder,
    audio_entropy_contexts *contexts, bit_stream *output);

// Unserialize quantized MDCT coefficients from the input bitstream.
evx_status audio_unserialize_coefficients(bit_stream *input,
    entropy_coder *coder, audio_entropy_contexts *contexts,
    bit_stream *feed_stream, int16 last_dc, int16 *coeffs);

// Returns the entropy band index (0-6) for a given coefficient position (1-511).
// Band 0 corresponds to the dc_ctx and is not returned here.
uint8 audio_coeff_to_band(uint16 position);

} // namespace evx

#endif // __EVX_AUDIO_SERIALIZE_H__
