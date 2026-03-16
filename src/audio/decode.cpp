
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

class evx3_audio_decoder_impl : public evx3_audio_decoder
{
    bool initialized;
    evx_audio_header header;

    // Sine window table.
    float32 window[EVX_AUDIO_WINDOW_SIZE];

    // Quantization matrix and current step.
    float32 quant_matrix[EVX_AUDIO_FREQ_COEFFS];

    // Entropy coding state.
    entropy_coder arith_coder;
    bit_stream feed_stream;
    audio_entropy_contexts entropy_ctx[EVX_AUDIO_MAX_CHANNELS];

    // Inter-frame DC delta state.
    int16 last_dc[EVX_AUDIO_MAX_CHANNELS];

    // Overlap-add state for reconstruction.
    audio_overlap_state overlap;

    // Working buffers.
    int16 quantized[EVX_AUDIO_FREQ_COEFFS];
    float32 dequantized[EVX_AUDIO_FREQ_COEFFS];
    float32 imdct_out[EVX_AUDIO_WINDOW_SIZE];
    float32 channel_pcm[EVX_AUDIO_MAX_CHANNELS][EVX_AUDIO_HOP_SIZE];

public:

    evx3_audio_decoder_impl()
    {
        initialized = false;
        clear_audio_header(&header);
        audio_generate_sine_window(window);
        feed_stream.resize_capacity(64 * EVX_KB * 8);
        clear();
    }

    virtual ~evx3_audio_decoder_impl()
    {
    }

    virtual evx_status clear() override
    {
        arith_coder.clear();
        feed_stream.empty();
        overlap.clear();

        memset(last_dc, 0, sizeof(last_dc));

        for (uint8 ch = 0; ch < EVX_AUDIO_MAX_CHANNELS; ch++)
            entropy_ctx[ch].clear();

        initialized = false;

        return EVX_SUCCESS;
    }

    virtual evx_status decode(bit_stream *input, float32 *pcm_output,
                               uint32 *sample_count) override
    {
        if (EVX_PARAM_CHECK)
        {
            if (!input || !pcm_output || !sample_count)
            {
                return evx_post_error(EVX_ERROR_INVALIDARG);
            }
        }

        // Reset feed_stream indices to prevent capacity overflow.
        // (Each decode_ctx/read_bit pair leaves occupancy at zero, but
        //  indices grow monotonically and eventually exceed capacity.)
        feed_stream.empty();

        // Read frame descriptor.
        evx_audio_frame_desc desc;
        evx_status result = input->read_bytes(&desc, sizeof(desc));
        if (evx_failed(result)) return result;

        // On first frame, set up from the header embedded in the stream
        // or rely on prior initialization.
        float32 quant_step = audio_quality_to_step(desc.quality);
        uint8 channels = header.channels;

        if (!initialized && channels == 0)
        {
            // Default to mono if no header has been set.
            channels = 1;
        }

        if (channels == 0)
        {
            return evx_post_error(EVX_ERROR_NOT_READY);
        }

        // Read coded data size and payload.
        uint32 coded_bytes = 0;
        result = input->read_bytes(&coded_bytes, sizeof(coded_bytes));
        if (evx_failed(result)) return result;

        bit_stream coded_stream;
        coded_stream.resize_capacity(coded_bytes * 8 + 64);

        if (coded_bytes > 0)
        {
            // Read the coded payload.
            uint8 *temp = new uint8[coded_bytes];
            result = input->read_bytes(temp, coded_bytes);
            if (evx_failed(result))
            {
                delete[] temp;
                return result;
            }
            coded_stream.write_bytes(temp, coded_bytes);
            delete[] temp;
        }

        // Start arithmetic decoder.
        arith_coder.clear();
        arith_coder.start_decode(&coded_stream);

        // Per-channel: unserialize -> dequantize -> IMDCT -> overlap-add.
        for (uint8 ch = 0; ch < channels; ch++)
        {
            result = audio_unserialize_coefficients(&coded_stream, &arith_coder,
                &entropy_ctx[ch], &feed_stream, last_dc[ch], quantized);
            if (evx_failed(result)) return result;

            last_dc[ch] = quantized[0];

            // Dequantize.
            if (!initialized)
            {
                // Need to initialize quant matrix on first decode.
                audio_bark_bands bands;
                audio_init_bark_bands(header.sample_rate > 0 ? header.sample_rate : 44100, &bands);
                audio_generate_quant_matrix(bands, quant_matrix);
                initialized = true;
            }

            audio_dequantize(quantized, quant_matrix, quant_step, dequantized);

            // Inverse MDCT.
            audio_mdct_inverse(dequantized, imdct_out);

            // Overlap-add to produce output PCM.
            audio_overlap_add(imdct_out, window, channel_pcm[ch],
                              overlap.prev_samples[ch]);
        }

        // Convert M/S back to L/R if stereo.
#if EVX_AUDIO_ENABLE_MID_SIDE
        if (channels == 2)
        {
            for (int32 n = 0; n < EVX_AUDIO_HOP_SIZE; n++)
            {
                float32 mid = channel_pcm[0][n];
                float32 side = channel_pcm[1][n];
                channel_pcm[0][n] = mid + side;
                channel_pcm[1][n] = mid - side;
            }
        }
#endif

        // Interleave output: L R L R ... for stereo, or L L L ... for mono.
        for (int32 n = 0; n < EVX_AUDIO_HOP_SIZE; n++)
        {
            for (uint8 ch = 0; ch < channels; ch++)
            {
                pcm_output[n * channels + ch] = channel_pcm[ch][n];
            }
        }

        *sample_count = desc.sample_count;

        return EVX_SUCCESS;
    }

    // Allow the factory to set the header before first decode.
    void set_header(const evx_audio_header &h)
    {
        header = h;
        audio_bark_bands bands;
        audio_init_bark_bands(header.sample_rate, &bands);
        audio_generate_quant_matrix(bands, quant_matrix);
        initialized = true;
    }

private:

    EVX_DISABLE_COPY_AND_ASSIGN(evx3_audio_decoder_impl);
};

// Implementation access for factory function.
evx3_audio_decoder *create_audio_decoder_impl()
{
    return new evx3_audio_decoder_impl();
}

void destroy_audio_decoder_impl(evx3_audio_decoder *decoder)
{
    delete static_cast<evx3_audio_decoder_impl *>(decoder);
}

// Internal access for setting decoder header.
void audio_decoder_set_header(evx3_audio_decoder *decoder, const evx_audio_header &header)
{
    evx3_audio_decoder_impl *impl = static_cast<evx3_audio_decoder_impl *>(decoder);
    impl->set_header(header);
}

} // namespace evx
