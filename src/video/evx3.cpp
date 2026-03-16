
#include "evx3.h"
#include "evx3enc.h"
#include "evx3dec.h"

namespace evx {

evx_status create_encoder(evx3_encoder **output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    (*output) = new evx3_encoder_impl;

    if (!(*output))
    {
        return evx_post_error(EVX_ERROR_OUTOFMEMORY);
    }

    return EVX_SUCCESS;
}

evx_status create_decoder(evx3_decoder **output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    (*output) = new evx3_decoder_impl;

    if (!(*output))
    {
        return evx_post_error(EVX_ERROR_OUTOFMEMORY);
    }

    return EVX_SUCCESS;
}

evx_status destroy_encoder(evx3_encoder *input)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    evx3_encoder_impl *internal = (evx3_encoder_impl *) input;

    delete internal;

    return EVX_SUCCESS;
}

evx_status destroy_decoder(evx3_decoder *input)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    evx3_decoder_impl *internal = (evx3_decoder_impl *) input;

    delete internal;

    return EVX_SUCCESS;
}

} // namespace evx