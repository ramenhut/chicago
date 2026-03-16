
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// audio_types.h
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

#ifndef __EVX_AUDIO_TYPES_H__
#define __EVX_AUDIO_TYPES_H__

#include "base.h"
#include "config.h"

namespace evx {

#pragma pack(push)
#pragma pack(2)

typedef struct evx_audio_header
{
    uint8 magic[4];        // must be 'EVA3'
    uint16 header_size;
    uint16 version;
    uint32 sample_rate;    // 44100 or 48000
    uint8 channels;        // 1 or 2
    uint8 bit_depth;       // 16 (input PCM depth)
    uint8 quality;
    uint8 reserved;

} evx_audio_header;

typedef struct evx_audio_frame_desc
{
    uint16 frame_index;
    uint16 sample_count;   // PCM samples produced per channel (normally 512)
    uint8 quality;

} evx_audio_frame_desc;

#pragma pack(pop)

evx_status initialize_audio_header(uint32 sample_rate, uint8 channels, evx_audio_header *header);
evx_status verify_audio_header(const evx_audio_header &header);
evx_status clear_audio_header(evx_audio_header *header);

} // namespace evx

#endif // __EVX_AUDIO_TYPES_H__
