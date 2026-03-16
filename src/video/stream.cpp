
#include "stream.h"
#include "golomb.h"
#include "scan.h"

namespace evx {

evx_status stream_encode_huffman_value(uint8 value, bit_stream *output)
{
    if (value >= 8)
    {
        // This simple precoder only supports values [0:7].
        return evx_post_error(EVX_ERROR_INVALIDARG);
    }

    uint8 bit = 1;
    uint8 count = 0;
    bit <<= value;

    while (bit) 
    {
        output->write_bit(bit & 0x1);
        bit >>= 0x1;
        count++;

        if (count >= 7) break;
    }

    return EVX_SUCCESS;
}

evx_status stream_decode_huffman_value(bit_stream *data, uint8 *output)
{
    uint8 value = 0;

    for (uint8 i = 0; i < 7; i++)
    {
        uint8 bit = 0;
        data->read_bit(&bit);

        if (bit) break;
        value++;
    }

    return value;
}

evx_status stream_encode_huffman_values(uint8 *data, uint32 count, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!data|| 0 == count || !output || 0 == output->query_capacity())
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint8 i = 0; i < count; i++)
    {
        if (evx_failed(stream_encode_huffman_value(data[i], output)))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

evx_status stream_decode_huffman_values(bit_stream *data, uint32 count, uint8 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if ( !data || 0 == data->query_occupancy() || !output || 0 == count)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 i = 0; i < count; ++i)
    {
        if (EVX_SUCCESS != stream_decode_huffman_value(data, output+i))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

evx_status stream_encode_value(uint16 value, bit_stream *out_buffer)
{
    if (EVX_PARAM_CHECK)
    {
        if (!out_buffer|| 0 == out_buffer->query_capacity())
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    uint8 count = 0;
    uint32 golomb_value = encode_unsigned_golomb_value(value, &count);

    return out_buffer->write_bits(&golomb_value, count);
}

evx_status stream_encode_value(int16 value, bit_stream *out_buffer)
{
    if (EVX_PARAM_CHECK)
    {
        if (!out_buffer|| 0 == out_buffer->query_capacity())
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    uint8 count = 0;
    uint32 golomb_value = encode_signed_golomb_value(value, &count);

    return out_buffer->write_bits(&golomb_value, count);
}

evx_status stream_encode_values(uint16 *data, uint32 count, bit_stream *out_buffer)
{
    if (EVX_PARAM_CHECK)
    {
        if (!data|| 0 == count || !out_buffer || 0 == out_buffer->query_capacity())
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 i = 0; i < count; ++i)
    {
        if (EVX_SUCCESS != stream_encode_value(data[i], out_buffer))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

evx_status stream_encode_values(int16 *data, uint32 count, bit_stream *out_buffer)
{
    if (EVX_PARAM_CHECK)
    {
        if (!data|| 0 == count || !out_buffer || 0 == out_buffer->query_capacity())
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 i = 0; i < count; ++i)
    {
        if (EVX_SUCCESS != stream_encode_value(data[i], out_buffer))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

evx_status stream_decode_value(bit_stream *data, uint16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!data || 0 == data->query_occupancy() || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    uint32 value = 0;
    uint8 count = evx_min2(32, data->query_occupancy());

    evx_status result = data->peek_bits(&value, count);
    *output = decode_unsigned_golomb_value(value, &count);
    data->seek(count);

    return result;
}

evx_status stream_decode_value(bit_stream *data, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!data || 0 == data->query_occupancy() || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    uint32 value = 0;
    uint8 count = evx_min2(32, data->query_occupancy());

    evx_status result = data->peek_bits(&value, count);
    *output = decode_signed_golomb_value(value, &count);
    data->seek(count);

    return result;
}

evx_status stream_decode_values(bit_stream *data, uint32 count, uint16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if ( !data || 0 == data->query_occupancy() || !output || 0 == count)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 i = 0; i < count; ++i)
    {
        if (EVX_SUCCESS != stream_decode_value(data, output+i))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

evx_status stream_decode_values(bit_stream *data, uint32 count, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if ( !data || 0 == data->query_occupancy() || !output || 0 == count)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 i = 0; i < count; ++i)
    {
        if (EVX_SUCCESS != stream_decode_value(data, output+i))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_encode_value(uint16 value, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!feed_stream || !coder || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (EVX_SUCCESS != stream_encode_value(value, feed_stream))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != coder->encode(feed_stream, output, false))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_encode_value(int16 value, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!feed_stream || !coder || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (EVX_SUCCESS != stream_encode_value(value, feed_stream))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != coder->encode(feed_stream, output, false))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_decode_value(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, uint16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !feed_stream|| !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    uint16 result = 0; 
    uint8 zero_count = 0;
    uint8 bit_count = 0;
    uint8 bit_value = 0;

    if (EVX_SUCCESS != coder->decode(1, input, feed_stream, false))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    while (!bit_value) 
    {
        zero_count++;

        if (EVX_SUCCESS != coder->decode(1, input, feed_stream, false))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

        if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    bit_count = zero_count + 1;

    for (uint8 i = 0; i < bit_count; i++) 
    {
        result <<= 1;
        result |= bit_value & 0x1;

        if (i < bit_count - 1)
        {
            if (EVX_SUCCESS != coder->decode(1, input, feed_stream, false))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }

            if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }
        }
    }

    result -= 1;
    *output = result;

    return EVX_SUCCESS;
}

evx_status entropy_stream_decode_value(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !feed_stream|| !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    int16 result = 0;
    uint8 zero_count = 0;
    uint8 bit_count = 0;
    uint8 bit_value = 0;
    int16 sign = 0;

    if (EVX_SUCCESS != coder->decode(1, input, feed_stream, false))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    while (!bit_value) 
    {
        zero_count++;
        
        if (EVX_SUCCESS != coder->decode(1, input, feed_stream, false))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

        if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    bit_count = zero_count + 1;

    for (uint8 i = 0; i < bit_count; i++) 
    {
        result <<= 1;
        result |= bit_value & 0x1;

        if (i < bit_count - 1)
        {
            if (EVX_SUCCESS != coder->decode(1, input, feed_stream, false))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }

            if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }
        }
    }

    // Remove the lowest bit as our sign bit.
    sign = 1 - 2 * (result & 0x1);
    result = sign * ((result >> 1) & 0x7FFF);

    // Defend against overflow on min int16.
    bit_count += zero_count;

    if (bit_count > 0x20) 
    {
        result |= 0x8000;
    }

    *output = result;

    return EVX_SUCCESS;
}

evx_status entropy_stream_encode_4x4(int16 *input, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !feed_stream || !coder || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 read_index = 0; read_index < 16; ++read_index)
    {
        // It's very important that we encode these as signed integers. { -2, -1, 0, 1, 2 }
        // are highly likely values in a prediction block, so we encode them with a signed
        // exp-golomb coder.

        entropy_stream_encode_value(input[EVX_MACROBLOCK_4x4_ZIGZAG[read_index]], feed_stream, coder, output);
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_encode_8x8(int16 *input, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !feed_stream || !coder || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 read_index = 0; read_index < 64; ++read_index)
    {
        entropy_stream_encode_value(input[EVX_MACROBLOCK_8x8_ZIGZAG[read_index]], feed_stream, coder, output);
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_encode_16x16(int16 *input, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !feed_stream || !coder || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 read_index = 0; read_index < 256; ++read_index)
    {
        entropy_stream_encode_value(input[EVX_MACROBLOCK_16x16_ZIGZAG[read_index]], feed_stream, coder, output);
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_decode_4x4(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !feed_stream || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 read_index = 0; read_index < 16; ++read_index)
    {
        entropy_stream_decode_value(input, coder, feed_stream, &(output[EVX_MACROBLOCK_4x4_ZIGZAG[read_index]]));
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_decode_8x8(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !feed_stream || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 read_index = 0; read_index < 64; ++read_index)
    {
        entropy_stream_decode_value(input, coder, feed_stream, &(output[EVX_MACROBLOCK_8x8_ZIGZAG[read_index]]));
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_decode_16x16(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !feed_stream || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    for (uint32 read_index = 0; read_index < 256; ++read_index)
    {
        entropy_stream_decode_value(input, coder, feed_stream, &(output[EVX_MACROBLOCK_16x16_ZIGZAG[read_index]]));
    }

    return EVX_SUCCESS;
}

evx_status entropy_rle_stream_encode_8x8(int16 *input, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !feed_stream || !coder || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    // Find the last non-zero coefficient in zigzag order.
    int32 last_sig = -1;
    for (int32 i = 63; i >= 0; --i)
    {
        if (input[EVX_MACROBLOCK_8x8_ZIGZAG[i]])
        {
            last_sig = i;
            break;
        }
    }

    // Encode last_significant_position + 1 (0 means all-zero block).
    entropy_stream_encode_value((uint16)(last_sig + 1), feed_stream, coder, output);

    if (last_sig < 0)
    {
        return EVX_SUCCESS;
    }

    // Walk zigzag positions 0..last_sig, emitting (zero_run, abs_value-1, sign) tokens.
    int32 pos = 0;
    while (pos <= last_sig)
    {
        // Count consecutive zeros.
        uint16 zero_run = 0;
        while (pos <= last_sig && input[EVX_MACROBLOCK_8x8_ZIGZAG[pos]] == 0)
        {
            zero_run++;
            pos++;
        }

        // Encode the zero run length.
        entropy_stream_encode_value(zero_run, feed_stream, coder, output);

        // Encode the non-zero coefficient.
        int16 coeff = input[EVX_MACROBLOCK_8x8_ZIGZAG[pos]];
        uint16 abs_val = (uint16)(abs((int32)coeff) - 1);
        uint8 sign_bit = (coeff < 0) ? 1 : 0;

        entropy_stream_encode_value(abs_val, feed_stream, coder, output);

        feed_stream->write_bit(sign_bit);
        coder->encode(feed_stream, output, false);

        pos++;
    }

    return EVX_SUCCESS;
}

evx_status entropy_rle_stream_decode_8x8(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !feed_stream || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    memset(output, 0, sizeof(int16) * 64);

    // Decode last_significant_position + 1.
    uint16 last_sig_plus1 = 0;
    entropy_stream_decode_value(input, coder, feed_stream, &last_sig_plus1);

    if (last_sig_plus1 == 0)
    {
        return EVX_SUCCESS;
    }

    int32 last_sig = (int32)last_sig_plus1 - 1;

    // Decode tokens: (zero_run, abs_value-1, sign).
    int32 pos = 0;
    while (pos <= last_sig)
    {
        uint16 zero_run = 0;
        entropy_stream_decode_value(input, coder, feed_stream, &zero_run);

        pos += zero_run;

        uint16 abs_val = 0;
        entropy_stream_decode_value(input, coder, feed_stream, &abs_val);

        uint8 sign_bit = 0;
        coder->decode(1, input, feed_stream, false);
        feed_stream->read_bit(&sign_bit);

        int16 coeff = (int16)(abs_val + 1);
        if (sign_bit) coeff = -coeff;

        output[EVX_MACROBLOCK_8x8_ZIGZAG[pos]] = coeff;
        pos++;
    }

    return EVX_SUCCESS;
}

static inline int select_coeff_context(int zigzag_pos)
{
    if (zigzag_pos == 0) return 0;
    if (zigzag_pos <= 10) return 1;
    if (zigzag_pos <= 40) return 2;
    return 3;
}

evx_status entropy_stream_encode_value_ctx(uint16 value, bit_stream *feed_stream, entropy_coder *coder, entropy_context *ctx, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!feed_stream || !coder || !ctx || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (EVX_SUCCESS != stream_encode_value(value, feed_stream))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != coder->encode_ctx(feed_stream, output, ctx))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_encode_value_ctx(int16 value, bit_stream *feed_stream, entropy_coder *coder, entropy_context *ctx, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!feed_stream || !coder || !ctx || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (EVX_SUCCESS != stream_encode_value(value, feed_stream))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != coder->encode_ctx(feed_stream, output, ctx))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return EVX_SUCCESS;
}

evx_status entropy_stream_decode_value_ctx(bit_stream *input, entropy_coder *coder, entropy_context *ctx, bit_stream *feed_stream, uint16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !ctx || !feed_stream || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    uint16 result = 0;
    uint8 zero_count = 0;
    uint8 bit_count = 0;
    uint8 bit_value = 0;

    if (EVX_SUCCESS != coder->decode_ctx(1, input, feed_stream, ctx))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    while (!bit_value)
    {
        zero_count++;

        if (EVX_SUCCESS != coder->decode_ctx(1, input, feed_stream, ctx))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

        if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    bit_count = zero_count + 1;

    for (uint8 i = 0; i < bit_count; i++)
    {
        result <<= 1;
        result |= bit_value & 0x1;

        if (i < bit_count - 1)
        {
            if (EVX_SUCCESS != coder->decode_ctx(1, input, feed_stream, ctx))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }

            if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }
        }
    }

    result -= 1;
    *output = result;

    return EVX_SUCCESS;
}

evx_status entropy_stream_decode_value_ctx(bit_stream *input, entropy_coder *coder, entropy_context *ctx, bit_stream *feed_stream, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !coder || !ctx || !feed_stream || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    int16 result = 0;
    uint8 zero_count = 0;
    uint8 bit_count = 0;
    uint8 bit_value = 0;
    int16 sign = 0;

    if (EVX_SUCCESS != coder->decode_ctx(1, input, feed_stream, ctx))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    while (!bit_value)
    {
        zero_count++;

        if (EVX_SUCCESS != coder->decode_ctx(1, input, feed_stream, ctx))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

        if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    bit_count = zero_count + 1;

    for (uint8 i = 0; i < bit_count; i++)
    {
        result <<= 1;
        result |= bit_value & 0x1;

        if (i < bit_count - 1)
        {
            if (EVX_SUCCESS != coder->decode_ctx(1, input, feed_stream, ctx))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }

            if (EVX_SUCCESS != feed_stream->read_bit(&bit_value))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }
        }
    }

    // Remove the lowest bit as our sign bit.
    sign = 1 - 2 * (result & 0x1);
    result = sign * ((result >> 1) & 0x7FFF);

    // Defend against overflow on min int16.
    bit_count += zero_count;

    if (bit_count > 0x20)
    {
        result |= 0x8000;
    }

    *output = result;

    return EVX_SUCCESS;
}

evx_status entropy_rle_stream_encode_8x8_ctx(int16 *input, const uint8 *scan_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !scan_table || !feed_stream || !coder || !contexts || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    // Find the last non-zero coefficient in scan order.
    int32 last_sig = -1;
    for (int32 i = 63; i >= 0; --i)
    {
        if (input[scan_table[i]])
        {
            last_sig = i;
            break;
        }
    }

    // Encode last_significant_position + 1 using DC context.
    entropy_stream_encode_value_ctx((uint16)(last_sig + 1), feed_stream, coder, &contexts[0], output);

    if (last_sig < 0)
    {
        return EVX_SUCCESS;
    }

    // Walk scan positions 0..last_sig, emitting (zero_run, abs_value-1, sign) tokens.
    int32 pos = 0;
    while (pos <= last_sig)
    {
        uint16 zero_run = 0;
        while (pos <= last_sig && input[scan_table[pos]] == 0)
        {
            zero_run++;
            pos++;
        }

        // Encode zero run with DC context.
        entropy_stream_encode_value_ctx(zero_run, feed_stream, coder, &contexts[0], output);

        // Select context by scan position for coefficient value.
        int ctx = select_coeff_context(pos);
        int16 coeff = input[scan_table[pos]];
        uint16 abs_val = (uint16)(abs((int32)coeff) - 1);
        uint8 sign_bit = (coeff < 0) ? 1 : 0;

        entropy_stream_encode_value_ctx(abs_val, feed_stream, coder, &contexts[ctx], output);

        feed_stream->write_bit(sign_bit);
        coder->encode_ctx(feed_stream, output, &contexts[ctx]);

        pos++;
    }

    return EVX_SUCCESS;
}

evx_status entropy_rle_stream_encode_8x8_ctx(int16 *input, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output)
{
    return entropy_rle_stream_encode_8x8_ctx(input, EVX_MACROBLOCK_8x8_ZIGZAG, feed_stream, coder, contexts, output);
}

evx_status entropy_rle_stream_decode_8x8_ctx(bit_stream *input, const uint8 *scan_table, entropy_coder *coder, entropy_context *contexts, bit_stream *feed_stream, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !scan_table || !coder || !contexts || !feed_stream || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    memset(output, 0, sizeof(int16) * 64);

    // Decode last_significant_position + 1 using DC context.
    uint16 last_sig_plus1 = 0;
    entropy_stream_decode_value_ctx(input, coder, &contexts[0], feed_stream, &last_sig_plus1);

    if (last_sig_plus1 == 0)
    {
        return EVX_SUCCESS;
    }

    int32 last_sig = (int32)last_sig_plus1 - 1;

    // Decode tokens: (zero_run, abs_value-1, sign).
    int32 pos = 0;
    while (pos <= last_sig)
    {
        uint16 zero_run = 0;
        entropy_stream_decode_value_ctx(input, coder, &contexts[0], feed_stream, &zero_run);

        pos += zero_run;

        int ctx = select_coeff_context(pos);

        uint16 abs_val = 0;
        entropy_stream_decode_value_ctx(input, coder, &contexts[ctx], feed_stream, &abs_val);

        uint8 sign_bit = 0;
        coder->decode_ctx(1, input, feed_stream, &contexts[ctx]);
        feed_stream->read_bit(&sign_bit);

        int16 coeff = (int16)(abs_val + 1);
        if (sign_bit) coeff = -coeff;

        output[scan_table[pos]] = coeff;
        pos++;
    }

    return EVX_SUCCESS;
}

evx_status entropy_rle_stream_decode_8x8_ctx(bit_stream *input, entropy_coder *coder, entropy_context *contexts, bit_stream *feed_stream, int16 *output)
{
    return entropy_rle_stream_decode_8x8_ctx(input, EVX_MACROBLOCK_8x8_ZIGZAG, coder, contexts, feed_stream, output);
}

// Helper: encode a single binary decision through a context
static inline void encode_bin(uint8 bin, bit_stream *feed_stream, entropy_coder *coder, entropy_context *ctx, bit_stream *output)
{
    feed_stream->write_bit(bin);
    coder->encode_ctx(feed_stream, output, ctx);
}

// Helper: decode a single binary decision through a context
static inline uint8 decode_bin(bit_stream *input, entropy_coder *coder, entropy_context *ctx, bit_stream *feed_stream)
{
    uint8 bin = 0;
    coder->decode_ctx(1, input, feed_stream, ctx);
    feed_stream->read_bit(&bin);
    return bin;
}

evx_status entropy_sigmap_encode_8x8_ctx(int16 *input, const uint8 *scan_table,
    bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !scan_table || !feed_stream || !coder || !contexts || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    // Find last non-zero coefficient in scan order.
    int32 last_sig = -1;
    for (int32 i = 63; i >= 0; --i)
    {
        if (input[scan_table[i]])
        {
            last_sig = i;
            break;
        }
    }

    // Encode last_sig+1 via Golomb through last_sig context group 0.
    entropy_stream_encode_value_ctx((uint16)(last_sig + 1), feed_stream, coder,
        &contexts[EVX_SIGMAP_CTX_LAST_BASE], output);

    if (last_sig < 0)
    {
        return EVX_SUCCESS;
    }

    // Significance map: for positions 0..last_sig-1, encode binary sig_flag.
    // Position last_sig is implicitly significant.
    for (int32 pos = 0; pos < last_sig; pos++)
    {
        uint8 sig = (input[scan_table[pos]] != 0) ? 1 : 0;
        encode_bin(sig, feed_stream, coder, &contexts[EVX_SIGMAP_CTX_SIG_BASE + pos], output);
    }

    // Level coding for each significant coefficient.
    bool seen_gt1 = false;
    for (int32 pos = 0; pos <= last_sig; pos++)
    {
        int16 coeff = input[scan_table[pos]];
        if (coeff == 0) continue;

        int group = select_coeff_context(pos);
        uint16 abs_level = (uint16)evx::abs(coeff);
        uint8 sign_bit = (coeff < 0) ? 1 : 0;

        // gt1 flag: is |coeff| > 1?
        uint8 gt1 = (abs_level > 1) ? 1 : 0;
        int gt1_state = seen_gt1 ? 1 : 0;
        encode_bin(gt1, feed_stream, coder,
            &contexts[EVX_SIGMAP_CTX_GT1_BASE + group * 2 + gt1_state], output);

        if (gt1)
        {
            seen_gt1 = true;

            // gt2 flag: is |coeff| > 2?
            uint8 gt2 = (abs_level > 2) ? 1 : 0;
            encode_bin(gt2, feed_stream, coder,
                &contexts[EVX_SIGMAP_CTX_GT2_BASE + group * 2 + gt1_state], output);

            if (gt2)
            {
                // Remainder: abs_level - 3 via Golomb
                entropy_stream_encode_value_ctx((uint16)(abs_level - 3), feed_stream, coder,
                    &contexts[EVX_SIGMAP_CTX_REMAINDER_BASE + group], output);
            }
        }

        // Sign bit via bypass (through coder's internal model)
        feed_stream->write_bit(sign_bit);
        coder->encode(feed_stream, output, false);
    }

    return EVX_SUCCESS;
}

evx_status entropy_sigmap_decode_8x8_ctx(bit_stream *input, const uint8 *scan_table,
    entropy_coder *coder, entropy_context *contexts, bit_stream *feed_stream, int16 *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!input || !scan_table || !coder || !contexts || !feed_stream || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    memset(output, 0, sizeof(int16) * 64);

    // Decode last_sig+1
    uint16 last_sig_plus1 = 0;
    entropy_stream_decode_value_ctx(input, coder, &contexts[EVX_SIGMAP_CTX_LAST_BASE], feed_stream, &last_sig_plus1);

    if (last_sig_plus1 == 0)
    {
        return EVX_SUCCESS;
    }

    int32 last_sig = (int32)last_sig_plus1 - 1;

    // Decode significance map: positions 0..last_sig-1
    // Position last_sig is implicitly significant.
    uint8 sig_map[64] = {};
    for (int32 pos = 0; pos < last_sig; pos++)
    {
        sig_map[pos] = decode_bin(input, coder, &contexts[EVX_SIGMAP_CTX_SIG_BASE + pos], feed_stream);
    }
    sig_map[last_sig] = 1;

    // Decode levels for significant coefficients.
    bool seen_gt1 = false;
    for (int32 pos = 0; pos <= last_sig; pos++)
    {
        if (!sig_map[pos]) continue;

        int group = select_coeff_context(pos);
        uint16 abs_level = 1;

        // gt1 flag
        int gt1_state = seen_gt1 ? 1 : 0;
        uint8 gt1 = decode_bin(input, coder,
            &contexts[EVX_SIGMAP_CTX_GT1_BASE + group * 2 + gt1_state], feed_stream);

        if (gt1)
        {
            seen_gt1 = true;
            abs_level = 2;

            // gt2 flag
            uint8 gt2 = decode_bin(input, coder,
                &contexts[EVX_SIGMAP_CTX_GT2_BASE + group * 2 + gt1_state], feed_stream);

            if (gt2)
            {
                // Remainder
                uint16 remainder = 0;
                entropy_stream_decode_value_ctx(input, coder,
                    &contexts[EVX_SIGMAP_CTX_REMAINDER_BASE + group], feed_stream, &remainder);
                abs_level = remainder + 3;
            }
        }

        // Sign bit via bypass
        uint8 sign_bit = 0;
        coder->decode(1, input, feed_stream, false);
        feed_stream->read_bit(&sign_bit);

        int16 coeff = (int16)abs_level;
        if (sign_bit) coeff = -coeff;

        output[scan_table[pos]] = coeff;
    }

    return EVX_SUCCESS;
}

} // namespace evx