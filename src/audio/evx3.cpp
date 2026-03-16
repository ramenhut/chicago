
#include "audio.h"
#include "types.h"

namespace evx {

// Forward declarations for implementation constructors and destructors.
evx3_audio_encoder *create_audio_encoder_impl(uint32 sample_rate, uint8 channels);
evx3_audio_decoder *create_audio_decoder_impl();
void destroy_audio_encoder_impl(evx3_audio_encoder *encoder);
void destroy_audio_decoder_impl(evx3_audio_decoder *decoder);
void audio_decoder_set_header(evx3_audio_decoder *decoder, const evx_audio_header &header);

evx_status create_audio_encoder(uint32 sample_rate, uint8 channels,
                                 evx3_audio_encoder **output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!output)
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

    (*output) = create_audio_encoder_impl(sample_rate, channels);

    if (!(*output))
    {
        return evx_post_error(EVX_ERROR_OUTOFMEMORY);
    }

    return EVX_SUCCESS;
}

evx_status create_audio_decoder(evx3_audio_decoder **output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    (*output) = create_audio_decoder_impl();

    if (!(*output))
    {
        return evx_post_error(EVX_ERROR_OUTOFMEMORY);
    }

    return EVX_SUCCESS;
}

evx_status destroy_audio_encoder(evx3_audio_encoder *input)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    destroy_audio_encoder_impl(input);

    return EVX_SUCCESS;
}

evx_status destroy_audio_decoder(evx3_audio_decoder *input)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    destroy_audio_decoder_impl(input);

    return EVX_SUCCESS;
}

} // namespace evx
