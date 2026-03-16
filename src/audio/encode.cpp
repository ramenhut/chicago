
#include "audio.h"
#include "config.h"
#include "types.h"
#include "transform.h"
#include "quantize.h"
#include "serialize.h"
#include "stream.h"
#include <cstring>
#include <cmath>

namespace evx {

class evx3_audio_encoder_impl : public evx3_audio_encoder
{
    bool initialized;
    evx_audio_header header;

    // Sine window table.
    float32 window[EVX_AUDIO_WINDOW_SIZE];

    // Input buffer: holds 1024 samples per channel (previous 512 + current 512).
    float32 input_buffer[EVX_AUDIO_MAX_CHANNELS][EVX_AUDIO_WINDOW_SIZE];

    // Quantization matrix and step.
    float32 quant_matrix[EVX_AUDIO_FREQ_COEFFS];
    float32 quant_step;

    // Entropy coding state.
    entropy_coder arith_coder;
    bit_stream feed_stream;
    audio_entropy_contexts entropy_ctx[EVX_AUDIO_MAX_CHANNELS];

    // Inter-frame DC delta state.
    int16 last_dc[EVX_AUDIO_MAX_CHANNELS];

    // Frame counter.
    uint16 frame_index;

    // Working buffers.
    float32 windowed[EVX_AUDIO_WINDOW_SIZE];
    float32 mdct_coeffs[EVX_AUDIO_FREQ_COEFFS];
    int16 quantized[EVX_AUDIO_FREQ_COEFFS];

public:

    evx3_audio_encoder_impl(uint32 sample_rate, uint8 channels)
    {
        initialized = false;
        initialize(sample_rate, channels);
    }

    virtual ~evx3_audio_encoder_impl()
    {
    }

    evx_status initialize(uint32 sample_rate, uint8 channels)
    {
        evx_status result = initialize_audio_header(sample_rate, channels, &header);
        if (evx_failed(result)) return result;

        audio_generate_sine_window(window);

        audio_bark_bands bands;
        audio_init_bark_bands(sample_rate, &bands);
        audio_generate_quant_matrix(bands, quant_matrix);

        quant_step = audio_quality_to_step(header.quality);

        feed_stream.resize_capacity(64 * EVX_KB * 8);

        memset(input_buffer, 0, sizeof(input_buffer));
        memset(last_dc, 0, sizeof(last_dc));

        for (uint8 ch = 0; ch < EVX_AUDIO_MAX_CHANNELS; ch++)
            entropy_ctx[ch].clear();

        frame_index = 0;
        initialized = true;

        return EVX_SUCCESS;
    }

    virtual evx_status clear() override
    {
        if (!initialized) return EVX_SUCCESS;

        arith_coder.clear();
        feed_stream.empty();

        memset(input_buffer, 0, sizeof(input_buffer));
        memset(last_dc, 0, sizeof(last_dc));

        for (uint8 ch = 0; ch < EVX_AUDIO_MAX_CHANNELS; ch++)
            entropy_ctx[ch].clear();

        frame_index = 0;

        return EVX_SUCCESS;
    }

    virtual evx_status set_quality(uint8 quality) override
    {
        if (quality > EVX_AUDIO_MAX_QUALITY)
            quality = EVX_AUDIO_MAX_QUALITY;

        header.quality = quality;
        quant_step = audio_quality_to_step(quality);

        return EVX_SUCCESS;
    }

    virtual evx_status encode(const float32 *pcm_input, uint32 sample_count,
                               bit_stream *output) override
    {
        if (EVX_PARAM_CHECK)
        {
            if (!pcm_input || !output)
            {
                return evx_post_error(EVX_ERROR_INVALIDARG);
            }

            if (sample_count != EVX_AUDIO_HOP_SIZE)
            {
                return evx_post_error(EVX_ERROR_INVALIDARG);
            }
        }

        uint8 channels = header.channels;
        const int32 hop = EVX_AUDIO_HOP_SIZE;

        // Buffer the new samples into the second half of the input buffer.
        // Input is interleaved: L R L R ... for stereo, or just L L L ... for mono.
        for (uint8 ch = 0; ch < channels; ch++)
        {
            for (int32 n = 0; n < hop; n++)
            {
                input_buffer[ch][hop + n] = pcm_input[n * channels + ch];
            }
        }

        // Write frame descriptor.
        evx_audio_frame_desc desc;
        desc.frame_index = frame_index;
        desc.sample_count = (uint16)sample_count;
        desc.quality = header.quality;
        output->write_bytes(&desc, sizeof(desc));

        // Start arithmetic coder for this frame.
        // We accumulate all channels into a single coded stream per frame.

        // Temporary output for the coded data.
        bit_stream coded_stream;
        coded_stream.resize_capacity(64 * EVX_KB * 8);

        // Mid/Side stereo transform (if stereo and enabled).
        float32 ms_buffer[EVX_AUDIO_MAX_CHANNELS][EVX_AUDIO_WINDOW_SIZE];

#if EVX_AUDIO_ENABLE_MID_SIDE
        if (channels == 2)
        {
            for (int32 n = 0; n < EVX_AUDIO_WINDOW_SIZE; n++)
            {
                float32 left = input_buffer[0][n];
                float32 right = input_buffer[1][n];
                ms_buffer[0][n] = (left + right) * 0.5f;
                ms_buffer[1][n] = (left - right) * 0.5f;
            }
        }
        else
#endif
        {
            for (uint8 ch = 0; ch < channels; ch++)
                memcpy(ms_buffer[ch], input_buffer[ch], sizeof(float32) * EVX_AUDIO_WINDOW_SIZE);
        }

        // Per-channel: window -> MDCT -> quantize -> serialize.
        for (uint8 ch = 0; ch < channels; ch++)
        {
            // Apply sine window.
            memcpy(windowed, ms_buffer[ch], sizeof(float32) * EVX_AUDIO_WINDOW_SIZE);
            audio_apply_window(windowed, window);

            // Forward MDCT.
            audio_mdct_forward(windowed, mdct_coeffs);

            // Quantize.
            audio_quantize(mdct_coeffs, quant_matrix, quant_step, quantized);

            // Serialize.
            evx_status result = audio_serialize_coefficients(quantized, last_dc[ch],
                &feed_stream, &arith_coder, &entropy_ctx[ch], &coded_stream);
            if (evx_failed(result)) return result;

            // Update last DC for delta coding.
            last_dc[ch] = quantized[0];
        }

        // Flush the arithmetic coder.
        arith_coder.finish_encode(&coded_stream);

        // Write coded data size + coded data.
        uint32 coded_bytes = coded_stream.query_byte_occupancy();
        output->write_bytes(&coded_bytes, sizeof(coded_bytes));
        output->write_bytes(coded_stream.query_data(), coded_bytes);

        // Slide buffer: move second half to first half for next frame.
        for (uint8 ch = 0; ch < channels; ch++)
        {
            memcpy(input_buffer[ch], input_buffer[ch] + hop, sizeof(float32) * hop);
        }

        frame_index++;

        return EVX_SUCCESS;
    }

    virtual evx_status flush(bit_stream *output) override
    {
        // No buffered data to flush in the current design.
        (void)output;
        return EVX_SUCCESS;
    }

private:

    EVX_DISABLE_COPY_AND_ASSIGN(evx3_audio_encoder_impl);
};

// Implementation access for factory function.
evx3_audio_encoder *create_audio_encoder_impl(uint32 sample_rate, uint8 channels)
{
    return new evx3_audio_encoder_impl(sample_rate, channels);
}

void destroy_audio_encoder_impl(evx3_audio_encoder *encoder)
{
    delete static_cast<evx3_audio_encoder_impl *>(encoder);
}

} // namespace evx
