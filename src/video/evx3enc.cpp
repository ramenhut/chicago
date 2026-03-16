
#include "evx3enc.h"
#include "config.h"
#include "convert.h"
#include "macroblock.h"
#include "evx_memory.h"
#include "quantize.h"

namespace evx {

evx_status engine_encode_frame(const image &input, const evx_frame &frame_desc, evx_context *context, bit_stream *output);

evx3_encoder_impl::evx3_encoder_impl()
{
    initialized = false;

    clear_frame(&frame);
    clear_header(&header);
#if EVX_ENABLE_B_FRAMES
    reorder_count = 0;
    display_order_counter = 0;
    ip_index = 0;
    first_frame = true;
#endif
}

evx3_encoder_impl::~evx3_encoder_impl()
{
    if (EVX_SUCCESS != clear())
    {
        evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
}

evx_status evx3_encoder_impl::clear()
{
    if (!initialized)
    {
        return EVX_SUCCESS;
    }

    clear_frame(&frame);
    clear_context(&context);

#if EVX_ENABLE_B_FRAMES
    reorder_count = 0;
    display_order_counter = 0;
    ip_index = 0;
    first_frame = true;
#endif

    initialized = false;

    return EVX_SUCCESS;
}

evx_status evx3_encoder_impl::insert_intra()
{
    frame.type = EVX_FRAME_INTRA;

    return EVX_SUCCESS;
}

evx_status evx3_encoder_impl::set_quality(uint8 quality)
{
    quality = clip_range(quality, 1, 31);

    // Remap user quality to internal QP with smooth quadratic curve above 16.
    // q 1-16: QP = q (unchanged)
    // q 17-31: QP = q + (q-16)^2 / 10, reaching QP~53 at q=31
    uint16 qp;
    if (quality <= 16)
        qp = quality;
    else
    {
        uint16 d = (uint16)quality - 16;
        qp = (uint16)quality + (d * d) / 10;
    }

    frame.quality = (uint16)evx_min2((int32)qp, (int32)(EVX_MAX_MPEG_QUANT_LEVELS - 1));
    return EVX_SUCCESS;
}

evx_status evx3_encoder_impl::initialize(uint32 width, uint32 height)
{
    if (initialized)
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    initialize_header(width, height, &header);

    uint32 aligned_width = align(width, EVX_MACROBLOCK_SIZE);
    uint32 aligned_height = align(height, EVX_MACROBLOCK_SIZE);

    if (EVX_SUCCESS != initialize_context(aligned_width, aligned_height, &context))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    initialized = true;
    return EVX_SUCCESS;
}

evx_status evx3_encoder_impl::encode(void *input, uint32 width, uint32 height, bit_stream *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!output || 0 == width || 0 == height || !input)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (!initialized)
    {
        if (evx_failed(initialize(width, height)))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

        if (evx_failed(output->write_bytes(&header, sizeof(evx_header))))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

#if EVX_ENABLE_B_FRAMES
        reorder_count = 0;
        display_order_counter = 0;
        ip_index = 0;
        first_frame = true;
#endif
    }

    if (width != header.frame_width || height != header.frame_height)
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

#if EVX_ENABLE_B_FRAMES
    if (EVX_B_FRAME_COUNT == 0)
    {
#endif
        if (evx_failed(output->write_bytes(&frame, sizeof(evx_frame))))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

        if (evx_failed(encode_frame(input, width, height, output)))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

#if EVX_ALLOW_INTER_FRAMES
        frame.type = EVX_FRAME_INTER;
#endif

        if (EVX_PERIODIC_INTRA_RATE)
        {
            if (0 == ((frame.index + 1) % EVX_PERIODIC_INTRA_RATE))
            {
                insert_intra();
            }
        }

        frame.index++;
#if EVX_ENABLE_B_FRAMES
    }
    else
    {
        // Periodic intra check: flush current GOP and reset to I-frame
        if (EVX_PERIODIC_INTRA_RATE && display_order_counter > 0 &&
            (display_order_counter % EVX_PERIODIC_INTRA_RATE == 0))
        {
            if (reorder_count > 0)
            {
                if (evx_failed(encode_gop(output)))
                    return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }
            frame.type = EVX_FRAME_INTRA;
            ip_index = 0;
        }

        // Detect explicit intra insertion: flush pending GOP and encode I immediately
        bool encode_as_intra = (frame.type == EVX_FRAME_INTRA);

        if (encode_as_intra && !first_frame && reorder_count > 0)
        {
            // Flush any pending frames before starting new intra GOP
            if (evx_failed(encode_gop(output)))
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            ip_index = 0;
        }

        // Buffer input frame
        uint32 frame_bytes = width * height * 3;
        reorder_buffer[reorder_count].pixels.resize(frame_bytes);
        memcpy(reorder_buffer[reorder_count].pixels.data(), input, frame_bytes);
        reorder_buffer[reorder_count].display_order = display_order_counter;
        reorder_count++;
        display_order_counter++;

        // First frame or forced intra: encode I immediately
        if (first_frame || encode_as_intra)
        {
            first_frame = false;
            frame.type = EVX_FRAME_INTRA;

            evx_frame i_frame = frame;
            i_frame.display_order = reorder_buffer[0].display_order;
            i_frame.dpb_slot = assign_ip_dpb_slot(ip_index);
            i_frame.is_reference = 1;
            i_frame.ref_slot_fwd = 0;
            i_frame.ref_slot_bwd = 0;

            if (evx_failed(encode_single_frame(reorder_buffer[0].pixels.data(),
                width, height, i_frame, output)))
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

            ip_index++;
            frame.type = EVX_FRAME_INTER;
            frame.index++;
            reorder_count = 0;
        }
        else if (reorder_count == EVX_B_FRAME_COUNT + 1)
        {
            if (evx_failed(encode_gop(output)))
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }
#endif

    return EVX_SUCCESS;
}

evx_status evx3_encoder_impl::encode_frame(void *input, uint32 width, uint32 height, bit_stream *output)
{
    image input_image;

    if (evx_failed(create_image(EVX_IMAGE_FORMAT_R8G8B8, input, width, height, &input_image)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return engine_encode_frame(input_image, frame, &context, output);
}

#if EVX_ENABLE_B_FRAMES
evx_status evx3_encoder_impl::encode_single_frame(void *input, uint32 width, uint32 height,
                                                    const evx_frame &frame_desc, bit_stream *output)
{
    // Encode frame header + data to a temporary stream so we can prefix the payload size.
    // This size prefix allows the decoder to read exactly one frame's worth of data
    // without ABAC lookahead bleeding into the adjacent frame.
    bit_stream temp;
    temp.resize_capacity(output->query_capacity());

    evx_frame frame_copy = frame_desc;
    if (evx_failed(temp.write_bytes(&frame_copy, sizeof(evx_frame))))
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

    image input_image;
    if (evx_failed(create_image(EVX_IMAGE_FORMAT_R8G8B8, input, width, height, &input_image)))
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

    if (evx_failed(engine_encode_frame(input_image, frame_desc, &context, &temp)))
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

    // Write 4-byte payload size prefix, then the frame data.
    uint32 payload_size = temp.query_byte_occupancy();
    if (evx_failed(output->write_bytes(&payload_size, sizeof(uint32))))
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    if (evx_failed(output->write_bytes(temp.query_data(), payload_size)))
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

    return EVX_SUCCESS;
}

evx_status evx3_encoder_impl::encode_gop(bit_stream *output)
{
    uint32 width = header.frame_width;
    uint32 height = header.frame_height;
    uint32 p_buf_idx = reorder_count - 1;

    // Step 1: Encode P-frame (anchor)
    {
        evx_frame p_frame = frame;
        p_frame.type = EVX_FRAME_INTER;
        p_frame.display_order = reorder_buffer[p_buf_idx].display_order;
        p_frame.dpb_slot = assign_ip_dpb_slot(ip_index);
        p_frame.is_reference = 1;
        p_frame.ref_slot_fwd = assign_ip_dpb_slot(ip_index - 1);
        p_frame.ref_slot_bwd = 0;

        if (evx_failed(encode_single_frame(reorder_buffer[p_buf_idx].pixels.data(),
            width, height, p_frame, output)))
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

        frame.index++;
    }

    uint32 b_count = reorder_count - 1;

    if (b_count > 0 && EVX_B_FRAME_COUNT >= 3)
    {
        // Hierarchical: middle B is reference
        uint32 ref_b_idx = b_count / 2;

        // Step 2: Encode reference B-frame
        {
            evx_frame b_frame = frame;
            b_frame.type = EVX_FRAME_BIDIR;
            b_frame.display_order = reorder_buffer[ref_b_idx].display_order;
            b_frame.dpb_slot = assign_bref_dpb_slot();
            b_frame.is_reference = 1;
            b_frame.ref_slot_fwd = assign_ip_dpb_slot(ip_index - 1);
            b_frame.ref_slot_bwd = assign_ip_dpb_slot(ip_index);
            b_frame.quality = evx_min2(frame.quality + 1, (uint16)31);

            if (evx_failed(encode_single_frame(reorder_buffer[ref_b_idx].pixels.data(),
                width, height, b_frame, output)))
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

            frame.index++;
        }

        // Step 3: Encode non-reference B-frames
        for (uint32 bi = 0; bi < b_count; bi++)
        {
            if (bi == ref_b_idx) continue;

            evx_frame b_frame = frame;
            b_frame.type = EVX_FRAME_BIDIR;
            b_frame.display_order = reorder_buffer[bi].display_order;
            b_frame.dpb_slot = assign_b_scratch_slot();
            b_frame.is_reference = 0;
            b_frame.quality = evx_min2(frame.quality + 2, (uint16)31);

            if (bi < ref_b_idx)
            {
                b_frame.ref_slot_fwd = assign_ip_dpb_slot(ip_index - 1);
                b_frame.ref_slot_bwd = assign_bref_dpb_slot();
            }
            else
            {
                b_frame.ref_slot_fwd = assign_bref_dpb_slot();
                b_frame.ref_slot_bwd = assign_ip_dpb_slot(ip_index);
            }

            if (evx_failed(encode_single_frame(reorder_buffer[bi].pixels.data(),
                width, height, b_frame, output)))
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

            frame.index++;
        }
    }
    else
    {
        // Non-hierarchical: all B-frames reference surrounding I/P
        for (uint32 bi = 0; bi < b_count; bi++)
        {
            evx_frame b_frame = frame;
            b_frame.type = EVX_FRAME_BIDIR;
            b_frame.display_order = reorder_buffer[bi].display_order;
            b_frame.dpb_slot = assign_b_scratch_slot();
            b_frame.is_reference = 0;
            b_frame.ref_slot_fwd = assign_ip_dpb_slot(ip_index - 1);
            b_frame.ref_slot_bwd = assign_ip_dpb_slot(ip_index);
            b_frame.quality = evx_min2(frame.quality + 2, (uint16)31);

            if (evx_failed(encode_single_frame(reorder_buffer[bi].pixels.data(),
                width, height, b_frame, output)))
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

            frame.index++;
        }
    }

    ip_index++;
    frame.type = EVX_FRAME_INTER;
    reorder_count = 0;
    return EVX_SUCCESS;
}

evx_status evx3_encoder_impl::flush(bit_stream *output)
{
    if (reorder_count == 0) return EVX_SUCCESS;

    if (reorder_count == 1)
    {
        evx_frame p_frame = frame;
        p_frame.type = EVX_FRAME_INTER;
        p_frame.display_order = reorder_buffer[0].display_order;
        p_frame.dpb_slot = assign_ip_dpb_slot(ip_index);
        p_frame.is_reference = 1;
        p_frame.ref_slot_fwd = assign_ip_dpb_slot(ip_index - 1);
        p_frame.ref_slot_bwd = 0;

        if (evx_failed(encode_single_frame(reorder_buffer[0].pixels.data(),
            header.frame_width, header.frame_height, p_frame, output)))
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

        frame.index++;
        ip_index++;
    }
    else
    {
        if (evx_failed(encode_gop(output)))
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    reorder_count = 0;
    return EVX_SUCCESS;
}
#endif

evx_status evx3_encoder_impl::peek(EVX_PEEK_STATE peek_state, void *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (!output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (!initialized)
    {
        return EVX_SUCCESS;
    }

    image output_image;

    if (evx_failed(create_image(EVX_IMAGE_FORMAT_R8G8B8, output, header.frame_width, header.frame_height, &output_image)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    switch (peek_state)
    {
        case EVX_PEEK_SOURCE: return convert_image(context.cache_bank.input_cache, &output_image);

        case EVX_PEEK_DESTINATION:
        {
#if EVX_ENABLE_B_FRAMES
            uint32 dest_index = frame.dpb_slot;
#else
            uint32 dest_index = query_prediction_index_by_offset(frame, 1);
#endif
            return convert_image(context.cache_bank.prediction_cache[dest_index], &output_image);
        }

        case EVX_PEEK_BLOCK_TABLE:
        {
            for (uint32 j = 0; j < header.frame_height; j++)
            for (uint32 i = 0; i < header.frame_width; i++)
            {
                uint8 *out_data = output_image.query_data() + output_image.query_block_offset(i, j);
                uint32 block_index = (i / EVX_MACROBLOCK_SIZE) + (j / EVX_MACROBLOCK_SIZE) * context.width_in_blocks;
                evx_block_desc *entry = &context.block_table[block_index];

                out_data[2] = 255 * EVX_IS_COPY_BLOCK_TYPE(entry->block_type);
                out_data[1] = 255 * EVX_IS_MOTION_BLOCK_TYPE(entry->block_type);
                out_data[0] = 255 * EVX_IS_INTRA_BLOCK_TYPE(entry->block_type);
            }

        } break;

        case EVX_PEEK_QUANT_TABLE:
        {
            for (uint32 j = 0; j < header.frame_height; j++)
            for (uint32 i = 0; i < header.frame_width; i++)
            {
                uint8 *out_data = output_image.query_data() + output_image.query_block_offset(i, j);
                uint32 block_index = (i / EVX_MACROBLOCK_SIZE) + (j / EVX_MACROBLOCK_SIZE) * context.width_in_blocks;
                evx_block_desc *entry = &context.block_table[block_index];

                if (!EVX_IS_COPY_BLOCK_TYPE(entry->block_type))
                {
                    out_data[0] = 255 - 15 * entry->q_index;
                    out_data[1] = 255 - 15 * entry->q_index;
                    out_data[2] = 255 - 15 * entry->q_index;
                }
                else
                {
                    out_data[0] = 255;
                    out_data[1] = 0;
                    out_data[2] = 0;
                }
            }

        } break;

        case EVX_PEEK_BLOCK_VARIANCE:
        {
            for (uint32 j = 0; j < header.frame_height; j++)
            for (uint32 i = 0; i < header.frame_width; i++)
            {
                uint8 *out_data = output_image.query_data() + output_image.query_block_offset(i, j);
                uint32 block_index = (i / EVX_MACROBLOCK_SIZE) + (j / EVX_MACROBLOCK_SIZE) * context.width_in_blocks;
                evx_block_desc *entry = &context.block_table[block_index];

                if (!EVX_IS_COPY_BLOCK_TYPE(entry->block_type))
                {
                    int16 value = clip_range(entry->variance / 30, 0, 255);

                    out_data[0] = value;
                    out_data[1] = value;
                    out_data[2] = value;
                }
                else
                {
                    out_data[0] = 255;
                    out_data[1] = 0;
                    out_data[2] = 0;
                }
            }
        } break;

        case EVX_PEEK_SPMP_TABLE:
        {
            for (uint32 j = 0; j < header.frame_height; j++)
            for (uint32 i = 0; i < header.frame_width; i++)
            {
                uint8 *out_data = output_image.query_data() + output_image.query_block_offset(i, j);
                uint32 block_index = (i / EVX_MACROBLOCK_SIZE) + (j / EVX_MACROBLOCK_SIZE) * context.width_in_blocks;
                evx_block_desc *entry = &context.block_table[block_index];

                if (!entry->sp_pred)
                {
                    out_data[0] = out_data[1] = out_data[2] = 0;
                }
                else
                {
                    out_data[0] = 0;
                    out_data[1] = 255 * entry->sp_amount;
                    out_data[2] = 255 * !entry->sp_amount;
                }
            }

        } break;

        default: return EVX_ERROR_NOTIMPL;
    }

    return EVX_SUCCESS;
}

} // namespace evx
