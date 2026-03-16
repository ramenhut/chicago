
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// evx3enc.h
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

#ifndef __EVX3_ENC_H__
#define __EVX3_ENC_H__

#include "evx3.h"
#include "common.h"
#include <vector>
#include <cstring>

namespace evx {

class evx3_encoder_impl : public evx3_encoder
{
    bool initialized;

    evx_frame frame;        // current frame state
    evx_header header;      // global video state
    evx_context context;    // encode context

#if EVX_ENABLE_B_FRAMES
    struct pending_frame {
        std::vector<uint8> pixels;  // RGB data copy
        uint32 display_order;
    };
    pending_frame reorder_buffer[EVX_B_FRAME_COUNT + 1];
    uint32 reorder_count;
    uint32 display_order_counter;
    uint32 ip_index;
    bool first_frame;
#endif

private:

    evx_status initialize(uint32 width, uint32 height);
    evx_status encode_frame(void *input, uint32 width, uint32 height, bit_stream *output);
#if EVX_ENABLE_B_FRAMES
    evx_status encode_gop(bit_stream *output);
    evx_status encode_single_frame(void *input, uint32 width, uint32 height,
                                    const evx_frame &frame_desc, bit_stream *output);
#endif

public:

    evx3_encoder_impl();
    virtual ~evx3_encoder_impl();

    evx_status clear();
    evx_status insert_intra();
    evx_status set_quality(uint8 quality);
    evx_status encode(void *input, uint32 width, uint32 height, bit_stream *output);
#if EVX_ENABLE_B_FRAMES
    evx_status flush(bit_stream *output) override;
#endif
    evx_status peek(EVX_PEEK_STATE peek_state, void *output);
};

} // namespace evx

#endif // __EVX3_ENC_H__
