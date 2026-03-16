
#include "types.h"
#include <cstring>

namespace evx {

evx_status initialize_audio_header(uint32 sample_rate, uint8 channels, evx_audio_header *header)
{
    if (EVX_PARAM_CHECK)
    {
        if (!header)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }

        if (channels < 1 || channels > EVX_AUDIO_MAX_CHANNELS)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }

        if (sample_rate == 0)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    memset(header, 0, sizeof(evx_audio_header));

    header->magic[0] = 'E';
    header->magic[1] = 'V';
    header->magic[2] = 'A';
    header->magic[3] = '3';
    header->header_size = sizeof(evx_audio_header);
    header->version = 1;
    header->sample_rate = sample_rate;
    header->channels = channels;
    header->bit_depth = 16;
    header->quality = EVX_AUDIO_DEFAULT_QUALITY;
    header->reserved = 0;

    return EVX_SUCCESS;
}

evx_status verify_audio_header(const evx_audio_header &header)
{
    if (header.magic[0] != 'E' || header.magic[1] != 'V' ||
        header.magic[2] != 'A' || header.magic[3] != '3')
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    if (header.header_size != sizeof(evx_audio_header))
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    if (header.channels < 1 || header.channels > EVX_AUDIO_MAX_CHANNELS)
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    if (header.sample_rate == 0)
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    return EVX_SUCCESS;
}

evx_status clear_audio_header(evx_audio_header *header)
{
    if (EVX_PARAM_CHECK)
    {
        if (!header)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    memset(header, 0, sizeof(evx_audio_header));

    return EVX_SUCCESS;
}

} // namespace evx
