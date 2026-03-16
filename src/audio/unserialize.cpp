
#include "serialize.h"
#include "stream.h"

namespace evx {

evx_status audio_unserialize_coefficients(bit_stream *input,
    entropy_coder *coder, audio_entropy_contexts *contexts,
    bit_stream *feed_stream, int16 last_dc, int16 *coeffs)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !contexts || !feed_stream || !coeffs)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    evx_status result = EVX_SUCCESS;

    // Decode DC coefficient.
    int16 dc_value = 0;
    result = entropy_stream_decode_value_ctx(input, coder, &contexts->dc_ctx,
                                              feed_stream, &dc_value);
    if (evx_failed(result)) return result;

#if EVX_AUDIO_ENABLE_DELTA_DC
    coeffs[0] = dc_value + last_dc;
#else
    coeffs[0] = dc_value;
#endif

    // Decode AC coefficients grouped by frequency band.
    for (int32 k = 1; k < EVX_AUDIO_FREQ_COEFFS; k++)
    {
        uint8 band = audio_coeff_to_band((uint16)k);
        entropy_context *ctx = &contexts->band_ctx[band];

        result = entropy_stream_decode_value_ctx(input, coder, ctx,
                                                  feed_stream, &coeffs[k]);
        if (evx_failed(result)) return result;
    }

    return EVX_SUCCESS;
}

} // namespace evx
