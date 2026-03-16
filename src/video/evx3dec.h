
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// evx3dec.h
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

#ifndef __EVX3_DEC_H__
#define __EVX3_DEC_H__

#include "evx3.h"
#include "common.h"
#include <vector>
#include <cstring>

namespace evx {

class evx3_decoder_impl : public evx3_decoder
{
    bool initialized;

    evx_frame frame;        // current frame state
    evx_header header;      // global video header
    evx_context context;    // decode context

#if EVX_ENABLE_B_FRAMES
    struct decoded_frame {
        std::vector<uint8> pixels;
        uint32 display_order;
        bool valid;
    };
    decoded_frame display_buffer[EVX_B_FRAME_COUNT + 2];
    uint32 display_buffer_size;
    uint32 next_display_order;
    bool output_ready;
#endif

private:

    evx_status initialize(bit_stream *input);
    evx_status read_frame_desc(bit_stream *input);
    evx_status decode_frame(bit_stream *input, void *output);

public:

    evx3_decoder_impl();
    virtual ~evx3_decoder_impl();

    evx_status clear();
    evx_status decode(bit_stream *input, void *output);

#if EVX_ENABLE_B_FRAMES
    evx_status flush(void *output) override;
    bool has_output() override;
#endif
};

} // namespace evx

#endif // __EVX3_DEC_H__