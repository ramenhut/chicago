
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// audio.h
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
// Description:
//
//   Audio codec for the EVX3 ecosystem. Uses MDCT transform, bark-scale
//   frequency-shaped quantization, and the existing ABAC entropy coder
//   to compress PCM audio.
//
// Additional Information:
//
//   For more information, visit http://www.bertolami.com.
*/

#ifndef __EVX_AUDIO_H__
#define __EVX_AUDIO_H__

#include "base.h"
#include "bitstream.h"

namespace evx {

class evx3_audio_encoder
{
protected:

    virtual ~evx3_audio_encoder() {}

public:

    // Clears the state of the encoder.
    virtual evx_status clear() = 0;

    // Sets a quality level between 0-31, with 0 being the highest quality.
    virtual evx_status set_quality(uint8 quality) = 0;

    // Encodes interleaved float32 PCM samples. sample_count is the number
    // of samples per channel (must be EVX_AUDIO_HOP_SIZE = 512).
    // Output bitstream receives the encoded audio frame.
    virtual evx_status encode(const float32 *pcm_input, uint32 sample_count,
                               bit_stream *output) = 0;

    // Flushes any remaining buffered data.
    virtual evx_status flush(bit_stream *output) = 0;
};

class evx3_audio_decoder
{
protected:

    virtual ~evx3_audio_decoder() {}

public:

    // Clears the state of the decoder.
    virtual evx_status clear() = 0;

    // Decodes a single audio frame from input. Writes interleaved float32 PCM
    // samples to pcm_output. sample_count receives the number of samples
    // per channel produced.
    virtual evx_status decode(bit_stream *input, float32 *pcm_output,
                               uint32 *sample_count) = 0;
};

// Factory functions — audio objects must be created and destroyed via these.
evx_status create_audio_encoder(uint32 sample_rate, uint8 channels,
                                 evx3_audio_encoder **output);
evx_status create_audio_decoder(evx3_audio_decoder **output);

evx_status destroy_audio_encoder(evx3_audio_encoder *input);
evx_status destroy_audio_decoder(evx3_audio_decoder *input);

} // namespace evx

#endif // __EVX_AUDIO_H__
