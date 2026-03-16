
#include "muxer.h"
#include <cstring>

namespace evx {

// ---------------------------------------------------------------------------
// evx_muxer
// ---------------------------------------------------------------------------

evx_muxer::evx_muxer()
{
    file_handle = nullptr;
    memset(&container_hdr, 0, sizeof(container_hdr));
    header_written = false;
}

evx_muxer::~evx_muxer()
{
    if (file_handle)
        close();
}

evx_status evx_muxer::open(const char *path)
{
    if (EVX_PARAM_CHECK)
    {
        if (!path)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    file_handle = fopen(path, "wb");
    if (!file_handle)
    {
        return evx_post_error(EVX_ERROR_IO_FAILURE);
    }

    container_hdr.magic[0] = 'E';
    container_hdr.magic[1] = 'V';
    container_hdr.magic[2] = 'X';
    container_hdr.magic[3] = '3';
    container_hdr.version = 1;
    container_hdr.stream_count = 0;

    streams.clear();
    header_written = false;

    return EVX_SUCCESS;
}

evx_status evx_muxer::add_video_stream(uint16 width, uint16 height, float32 frame_rate,
                                         uint8 *stream_id_out)
{
    if (EVX_PARAM_CHECK)
    {
        if (!stream_id_out || header_written)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    evx_stream_descriptor desc;
    memset(&desc, 0, sizeof(desc));

    desc.stream_type = EVX_STREAM_TYPE_VIDEO;
    desc.stream_id = (uint8)streams.size();
    desc.width = width;
    desc.height = height;
    desc.frame_rate = frame_rate;

    streams.push_back(desc);
    *stream_id_out = desc.stream_id;

    return EVX_SUCCESS;
}

evx_status evx_muxer::add_audio_stream(uint32 sample_rate, uint8 channels, uint8 bit_depth,
                                         uint8 quality, uint8 *stream_id_out)
{
    if (EVX_PARAM_CHECK)
    {
        if (!stream_id_out || header_written)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    evx_stream_descriptor desc;
    memset(&desc, 0, sizeof(desc));

    desc.stream_type = EVX_STREAM_TYPE_AUDIO;
    desc.stream_id = (uint8)streams.size();
    desc.sample_rate = sample_rate;
    desc.channels = channels;
    desc.bit_depth = bit_depth;
    desc.quality = quality;

    streams.push_back(desc);
    *stream_id_out = desc.stream_id;

    return EVX_SUCCESS;
}

evx_status evx_muxer::write_header()
{
    if (!file_handle || header_written)
    {
        return evx_post_error(EVX_ERROR_NOT_READY);
    }

    container_hdr.stream_count = (uint8)streams.size();
    container_hdr.header_size = (uint32)(sizeof(evx_container_header) +
                                streams.size() * sizeof(evx_stream_descriptor));

    if (fwrite(&container_hdr, sizeof(container_hdr), 1, file_handle) != 1)
    {
        return evx_post_error(EVX_ERROR_IO_FAILURE);
    }

    for (size_t i = 0; i < streams.size(); i++)
    {
        if (fwrite(&streams[i], sizeof(evx_stream_descriptor), 1, file_handle) != 1)
        {
            return evx_post_error(EVX_ERROR_IO_FAILURE);
        }
    }

    header_written = true;

    return EVX_SUCCESS;
}

evx_status evx_muxer::write_packet(uint8 stream_id, uint8 stream_type,
                                     uint64 timestamp, const void *payload, uint32 payload_size)
{
    if (EVX_PARAM_CHECK)
    {
        if (!file_handle || !header_written || !payload || payload_size == 0)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    evx_packet_header pkt_hdr;
    pkt_hdr.magic[0] = 'E';
    pkt_hdr.magic[1] = 'P';
    pkt_hdr.stream_id = stream_id;
    pkt_hdr.stream_type = stream_type;
    pkt_hdr.timestamp = timestamp;
    pkt_hdr.payload_size = payload_size;

    if (fwrite(&pkt_hdr, sizeof(pkt_hdr), 1, file_handle) != 1)
    {
        return evx_post_error(EVX_ERROR_IO_FAILURE);
    }

    if (fwrite(payload, payload_size, 1, file_handle) != 1)
    {
        return evx_post_error(EVX_ERROR_IO_FAILURE);
    }

    return EVX_SUCCESS;
}

evx_status evx_muxer::close()
{
    if (file_handle)
    {
        fclose(file_handle);
        file_handle = nullptr;
    }

    streams.clear();
    header_written = false;

    return EVX_SUCCESS;
}

// ---------------------------------------------------------------------------
// evx_demuxer
// ---------------------------------------------------------------------------

evx_demuxer::evx_demuxer()
{
    file_handle = nullptr;
    memset(&container_hdr, 0, sizeof(container_hdr));
}

evx_demuxer::~evx_demuxer()
{
    if (file_handle)
        close();
}

evx_status evx_demuxer::open(const char *path)
{
    if (EVX_PARAM_CHECK)
    {
        if (!path)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    file_handle = fopen(path, "rb");
    if (!file_handle)
    {
        return evx_post_error(EVX_ERROR_IO_FAILURE);
    }

    streams.clear();

    return EVX_SUCCESS;
}

evx_status evx_demuxer::read_header()
{
    if (!file_handle)
    {
        return evx_post_error(EVX_ERROR_NOT_READY);
    }

    if (fread(&container_hdr, sizeof(container_hdr), 1, file_handle) != 1)
    {
        return evx_post_error(EVX_ERROR_IO_FAILURE);
    }

    if (container_hdr.magic[0] != 'E' || container_hdr.magic[1] != 'V' ||
        container_hdr.magic[2] != 'X' || container_hdr.magic[3] != '3')
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    streams.resize(container_hdr.stream_count);

    for (uint8 i = 0; i < container_hdr.stream_count; i++)
    {
        if (fread(&streams[i], sizeof(evx_stream_descriptor), 1, file_handle) != 1)
        {
            return evx_post_error(EVX_ERROR_IO_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

uint8 evx_demuxer::query_stream_count() const
{
    return (uint8)streams.size();
}

const evx_stream_descriptor &evx_demuxer::query_stream(uint8 index) const
{
    return streams[index];
}

evx_status evx_demuxer::read_packet(evx_demux_packet *packet)
{
    if (EVX_PARAM_CHECK)
    {
        if (!file_handle || !packet)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    evx_packet_header pkt_hdr;
    if (fread(&pkt_hdr, sizeof(pkt_hdr), 1, file_handle) != 1)
    {
        return EVX_ERROR_OPERATION_COMPLETED;  // EOF
    }

    if (pkt_hdr.magic[0] != 'E' || pkt_hdr.magic[1] != 'P')
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    packet->stream_id = pkt_hdr.stream_id;
    packet->stream_type = pkt_hdr.stream_type;
    packet->timestamp = pkt_hdr.timestamp;
    packet->payload.resize(pkt_hdr.payload_size);

    if (pkt_hdr.payload_size > 0)
    {
        if (fread(packet->payload.data(), pkt_hdr.payload_size, 1, file_handle) != 1)
        {
            return evx_post_error(EVX_ERROR_IO_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

evx_status evx_demuxer::close()
{
    if (file_handle)
    {
        fclose(file_handle);
        file_handle = nullptr;
    }

    streams.clear();

    return EVX_SUCCESS;
}

} // namespace evx
