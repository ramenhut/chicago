
#include "serialize.h"
#include "stream.h"

namespace evx {

void audio_entropy_contexts::clear()
{
    dc_ctx.clear();

    for (int32 i = 0; i < EVX_AUDIO_ENTROPY_BAND_COUNT - 1; i++)
    {
        band_ctx[i].clear();
    }
}

uint8 audio_coeff_to_band(uint16 position)
{
    // Split 511 AC coefficients (positions 1-511) into 7 bands:
    //   band 0: 1-8     (sub-bass)
    //   band 1: 9-24    (bass)
    //   band 2: 25-56   (low-mid)
    //   band 3: 57-112  (mid)
    //   band 4: 113-192 (upper-mid)
    //   band 5: 193-320 (presence)
    //   band 6: 321-511 (brilliance)

    if (position <= 8)   return 0;
    if (position <= 24)  return 1;
    if (position <= 56)  return 2;
    if (position <= 112) return 3;
    if (position <= 192) return 4;
    if (position <= 320) return 5;
    return 6;
}

evx_status audio_serialize_coefficients(const int16 *coeffs, int16 last_dc,
    bit_stream *feed_stream, entropy_coder *coder,
    audio_entropy_contexts *contexts, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!coeffs || !feed_stream || !coder || !contexts || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    evx_status result = EVX_SUCCESS;

    // Encode DC coefficient (with delta coding if enabled).
    int16 dc_value = coeffs[0];

#if EVX_AUDIO_ENABLE_DELTA_DC
    dc_value = dc_value - last_dc;
#endif

    feed_stream->empty();
    result = entropy_stream_encode_value_ctx(dc_value, feed_stream, coder,
                                              &contexts->dc_ctx, output);
    if (evx_failed(result)) return result;

    // Encode AC coefficients grouped by frequency band.
    for (int32 k = 1; k < EVX_AUDIO_FREQ_COEFFS; k++)
    {
        uint8 band = audio_coeff_to_band((uint16)k);
        entropy_context *ctx = &contexts->band_ctx[band];

        feed_stream->empty();
        result = entropy_stream_encode_value_ctx(coeffs[k], feed_stream, coder,
                                                  ctx, output);
        if (evx_failed(result)) return result;
    }

    return EVX_SUCCESS;
}

} // namespace evx
