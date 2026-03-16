
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// muxer.h
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
//   Simple packet-based container format for interleaving audio and video
//   streams in the EVX ecosystem.
//
// Additional Information:
//
//   For more information, visit http://www.bertolami.com.
*/

#ifndef __EVX_MUXER_H__
#define __EVX_MUXER_H__

#include "base.h"
#include <cstdio>
#include <vector>

namespace evx {

#define EVX_STREAM_TYPE_VIDEO  (0)
#define EVX_STREAM_TYPE_AUDIO  (1)

#pragma pack(push)
#pragma pack(2)

struct evx_container_header
{
    uint8 magic[4];        // must be 'EVX3'
    uint32 header_size;
    uint8 version;         // 1
    uint8 stream_count;
};

struct evx_stream_descriptor
{
    uint8 stream_type;     // EVX_STREAM_TYPE_VIDEO or EVX_STREAM_TYPE_AUDIO
    uint8 stream_id;

    // Video fields (valid when stream_type == VIDEO).
    uint16 width;
    uint16 height;
    float32 frame_rate;

    // Audio fields (valid when stream_type == AUDIO).
    uint32 sample_rate;
    uint8 channels;
    uint8 bit_depth;
    uint8 quality;
};

struct evx_packet_header
{
    uint8 magic[2];        // must be 'EP'
    uint8 stream_id;
    uint8 stream_type;
    uint64 timestamp;      // microseconds
    uint32 payload_size;
};

#pragma pack(pop)

// Muxer: writes an interleaved container file.
class evx_muxer
{
    FILE *file_handle;
    evx_container_header container_hdr;
    std::vector<evx_stream_descriptor> streams;
    bool header_written;

public:

    evx_muxer();
    ~evx_muxer();

    evx_status open(const char *path);

    // Add a video stream. Returns the assigned stream_id.
    evx_status add_video_stream(uint16 width, uint16 height, float32 frame_rate,
                                 uint8 *stream_id_out);

    // Add an audio stream. Returns the assigned stream_id.
    evx_status add_audio_stream(uint32 sample_rate, uint8 channels, uint8 bit_depth,
                                 uint8 quality, uint8 *stream_id_out);

    // Write the container header (must be called after adding all streams).
    evx_status write_header();

    // Write a packet with the given payload.
    evx_status write_packet(uint8 stream_id, uint8 stream_type,
                             uint64 timestamp, const void *payload, uint32 payload_size);

    evx_status close();

private:

    EVX_DISABLE_COPY_AND_ASSIGN(evx_muxer);
};

// Packet received by the demuxer.
struct evx_demux_packet
{
    uint8 stream_id;
    uint8 stream_type;
    uint64 timestamp;
    std::vector<uint8> payload;
};

// Demuxer: reads an interleaved container file.
class evx_demuxer
{
    FILE *file_handle;
    evx_container_header container_hdr;
    std::vector<evx_stream_descriptor> streams;

public:

    evx_demuxer();
    ~evx_demuxer();

    evx_status open(const char *path);

    // Read the container header and stream descriptors.
    evx_status read_header();

    uint8 query_stream_count() const;
    const evx_stream_descriptor &query_stream(uint8 index) const;

    // Read the next packet. Returns EVX_ERROR_OPERATION_COMPLETED at EOF.
    evx_status read_packet(evx_demux_packet *packet);

    evx_status close();

private:

    EVX_DISABLE_COPY_AND_ASSIGN(evx_demuxer);
};

} // namespace evx

#endif // __EVX_MUXER_H__
