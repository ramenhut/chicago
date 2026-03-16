
#include "base.h"
#include "scan.h"
#include "common.h"
#include "stream.h"
#include "macroblock.h"
#include "motion.h"
#if EVX_ENABLE_SAO
#include "sao.h"
#endif

namespace evx {

evx_status unserialize_block_8x8(bit_stream *input, int16 last_dc, const uint8 *scan_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, int16 *cache, int16 *dest, uint32 dest_width)
{
#if EVX_ENABLE_SIGMAP_CODING
    entropy_sigmap_decode_8x8_ctx(input, scan_table, coder, contexts, feed_stream, cache);
#else
    entropy_rle_stream_decode_8x8_ctx(input, scan_table, coder, contexts, feed_stream, cache);
#endif

    cache[0] = cache[0] + last_dc;  // Reconstruct our dc using the delta value.

    for (uint32 j = 0; j < 8; j++)
    {
        aligned_byte_copy(cache + j * 8, sizeof(int16) * 8, dest + j * dest_width);
    }

    return EVX_SUCCESS;
}

evx_status unserialize_block_16x16(bit_stream *input, int16 last_dc, const uint8 *scan_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, int16 *cache, int16 *dest, uint32 dest_width)
{
    unserialize_block_8x8(input, last_dc, scan_table, feed_stream, coder, contexts, cache, dest, dest_width);
    unserialize_block_8x8(input, dest[0], scan_table, feed_stream, coder, contexts, cache, dest + 8, dest_width);
    unserialize_block_8x8(input, dest[0], scan_table, feed_stream, coder, contexts, cache, dest + 8 * dest_width, dest_width);
    unserialize_block_8x8(input, dest[8 * dest_width], scan_table, feed_stream, coder, contexts, cache, dest + 8 * dest_width + 8, dest_width);

    return EVX_SUCCESS;
}

evx_status unserialize_image_blocks_16x16(bit_stream *input, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, int16 *cache_data, image *dest_image)
{
    uint32 block_index = 0;
    uint32 width = dest_image->query_width();
    uint32 height = dest_image->query_height();

    feed_stream->empty();

    for (int32 j = 0; j < height; j += EVX_MACROBLOCK_SIZE)
    for (int32 i = 0; i < width; i += EVX_MACROBLOCK_SIZE)
    {
        evx_block_desc *block_desc = &block_table[block_index++];
        
        int16 last_dc = 0;
        int16 *last_block_data = NULL;
        int16 *block_data = reinterpret_cast<int16 *>(dest_image->query_data() + dest_image->query_block_offset(i, j));

        // Copy blocks contain no residuals.
        if (EVX_IS_COPY_BLOCK_TYPE(block_desc->block_type))
        {
            continue;
        }

        // Support delta dc coding
        if (i >= EVX_MACROBLOCK_SIZE)
        {
            last_block_data = reinterpret_cast<int16 *>(dest_image->query_data() + dest_image->query_block_offset(i - (EVX_MACROBLOCK_SIZE >> 1), j));
            last_dc = last_block_data[0];
        }
        else
        {
            if (j >= EVX_MACROBLOCK_SIZE)
            {
                // i is zero, so we sample from the block above.
                last_block_data = reinterpret_cast<int16 *>(dest_image->query_data() + dest_image->query_block_offset(i, j - (EVX_MACROBLOCK_SIZE >> 1)));
                last_dc = last_block_data[0];
            }
        }

        const uint8 *scan_table = (block_desc->block_type == EVX_BLOCK_INTRA_DEFAULT)
            ? select_scan_table_8x8(block_desc->intra_mode) : EVX_MACROBLOCK_8x8_ZIGZAG;
#if EVX_ENABLE_SIGMAP_CODING
        int ctx_offset = (block_desc->block_type == EVX_BLOCK_INTRA_DEFAULT) ? 0 : EVX_SIGMAP_CONTEXTS_PER_SET;
#elif EVX_ENABLE_SPLIT_ENTROPY_CONTEXTS
        int ctx_offset = (block_desc->block_type == EVX_BLOCK_INTRA_DEFAULT) ? 0 : 4;
#else
        int ctx_offset = 0;
#endif
        unserialize_block_16x16(input, last_dc, scan_table, feed_stream, coder, contexts + ctx_offset, cache_data, block_data, width);
    }

    return EVX_SUCCESS;
}

evx_status unserialize_image_blocks_8x8(bit_stream *input, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, int16 *cache_data, image *dest_image)
{
    uint32 block_index = 0;
    uint32 width = dest_image->query_width();
    uint32 height = dest_image->query_height();

    feed_stream->empty();
   
    for (int32 j = 0; j < height; j += (EVX_MACROBLOCK_SIZE >> 1))
    for (int32 i = 0; i < width; i += (EVX_MACROBLOCK_SIZE >> 1))
    {
        evx_block_desc *block_desc = &block_table[block_index++];

        int16 last_dc = 0;
        int16 *last_block_data = NULL;
        int16 *block_data = reinterpret_cast<int16 *>(dest_image->query_data() + dest_image->query_block_offset(i, j));

        // Copy blocks contain no residuals.
        if (EVX_IS_COPY_BLOCK_TYPE(block_desc->block_type))
        {
            continue;
        }

        // Support delta dc coding
        if (i >= (EVX_MACROBLOCK_SIZE >> 1))
        {
            last_block_data = reinterpret_cast<int16 *>(dest_image->query_data() + dest_image->query_block_offset(i - (EVX_MACROBLOCK_SIZE >> 1), j));
            last_dc = last_block_data[0];
        }
        else
        {
            if (j >= (EVX_MACROBLOCK_SIZE >> 1))
            {
                // i is zero, so we sample from the block above.
                last_block_data = reinterpret_cast<int16 *>(dest_image->query_data() + dest_image->query_block_offset(i, j - (EVX_MACROBLOCK_SIZE >> 1)));
                last_dc = last_block_data[0];
            }
        }

        const uint8 *scan_table = (block_desc->block_type == EVX_BLOCK_INTRA_DEFAULT)
            ? select_scan_table_8x8(block_desc->intra_mode) : EVX_MACROBLOCK_8x8_ZIGZAG;
#if EVX_ENABLE_SIGMAP_CODING
        int ctx_offset = (block_desc->block_type == EVX_BLOCK_INTRA_DEFAULT) ? 0 : EVX_SIGMAP_CONTEXTS_PER_SET;
#elif EVX_ENABLE_SPLIT_ENTROPY_CONTEXTS
        int ctx_offset = (block_desc->block_type == EVX_BLOCK_INTRA_DEFAULT) ? 0 : 4;
#else
        int ctx_offset = 0;
#endif
        unserialize_block_8x8(input, last_dc, scan_table, feed_stream, coder, contexts + ctx_offset, cache_data, block_data, width);
    }

    return EVX_SUCCESS;
}

evx_status unserialize_macroblocks(bit_stream *input, evx_context *context)
{
    image *y_image = context->cache_bank.input_cache.query_y_image();
    image *u_image = context->cache_bank.input_cache.query_u_image();
    image *v_image = context->cache_bank.input_cache.query_v_image();

    if (evx_failed(unserialize_image_blocks_16x16(input, context->block_table, &context->feed_stream,
                   &context->arith_coder, context->coeff_contexts, context->cache_bank.staging_block.data_y, y_image)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_CHROMA_SUPPORT

    if (evx_failed(unserialize_image_blocks_8x8(input, context->block_table, &context->feed_stream,
                   &context->arith_coder, context->coeff_contexts, context->cache_bank.staging_block.data_u, u_image)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(unserialize_image_blocks_8x8(input, context->block_table, &context->feed_stream,
                   &context->arith_coder, context->coeff_contexts, context->cache_bank.staging_block.data_v, v_image)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#endif

    return EVX_SUCCESS;
}

#if EVX_ENABLE_UNIFIED_METADATA

evx_status unserialize_block_table_unified(uint32 block_count, uint32 width_in_blocks, bit_stream *input,
    bit_stream *feed_stream, entropy_coder *coder, evx_metadata_contexts *meta,
#if EVX_ENABLE_TEMPORAL_MVP
    const evx_block_desc *prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
    bool is_bidir_frame,
#endif
    evx_block_desc *block_table)
{
    int16 last_q = 0;

    for (uint32 i = 0; i < block_count; i++)
    {
        evx_block_desc *bd = &block_table[i];

        // Block type: 3 bits via block_type_ctx
        coder->decode_ctx(3, input, feed_stream, &meta->block_type_ctx);
        feed_stream->read_bits(&bd->block_type, 3);

        // Intra mode for INTRA_DEFAULT blocks
        if (bd->block_type == EVX_BLOCK_INTRA_DEFAULT)
        {
#if EVX_ENABLE_MPM
            uint8 mpm[2];
            compute_intra_mpm(block_table, width_in_blocks, i, mpm);
            bool mpm_hit = false;
            coder->decode_ctx(1, input, feed_stream, &meta->mpm_flag_ctx);
            feed_stream->read_bit(&mpm_hit);
            if (mpm_hit)
            {
                bool mpm_idx = false;
                coder->decode_ctx(1, input, feed_stream, &meta->mpm_idx_ctx);
                feed_stream->read_bit(&mpm_idx);
                bd->intra_mode = mpm_idx ? mpm[1] : mpm[0];
            }
            else
            {
                coder->decode_ctx(3, input, feed_stream, &meta->intra_mode_ctx);
                feed_stream->read_bits(&bd->intra_mode, 3);
            }
#else
            coder->decode_ctx(3, input, feed_stream, &meta->intra_mode_ctx);
            feed_stream->read_bits(&bd->intra_mode, 3);
#endif
        }

        // Prediction target for inter blocks
        if (!EVX_IS_INTRA_BLOCK_TYPE(bd->block_type))
        {
            uint8 bit_count = log2((uint8) EVX_REFERENCE_FRAME_COUNT);
            coder->decode_ctx(bit_count, input, feed_stream, &meta->pred_target_ctx);
            feed_stream->read_bits(&bd->prediction_target, bit_count);
        }

        // Motion vectors: decode X and Y together (per-block, both components available for predictor)
        if (EVX_IS_MOTION_BLOCK_TYPE(bd->block_type))
        {
            int16 pred_x, pred_y;
            compute_mv_predictor(block_table, width_in_blocks, i,
#if EVX_ENABLE_TEMPORAL_MVP
                                  prev_block_table,
#endif
                                  &pred_x, &pred_y);

            int16 mvd_x = 0;
            entropy_stream_decode_value_ctx(input, coder, &meta->mvd_x_ctx, feed_stream, &mvd_x);
            bd->motion_x = mvd_x + pred_x;

            int16 mvd_y = 0;
            entropy_stream_decode_value_ctx(input, coder, &meta->mvd_y_ctx, feed_stream, &mvd_y);
            bd->motion_y = mvd_y + pred_y;

            // Subpixel prediction enabled bit
            coder->decode_ctx(1, input, feed_stream, &meta->sp_pred_ctx);
            feed_stream->read_bit(&bd->sp_pred);

            if (bd->sp_pred)
            {
                coder->decode_ctx(1, input, feed_stream, &meta->sp_amount_ctx);
                feed_stream->read_bit(&bd->sp_amount);

                coder->decode_ctx(3, input, feed_stream, &meta->sp_index_ctx);
                feed_stream->read_bits(&bd->sp_index, 3);
            }
        }

        // B-frame prediction mode and secondary reference/MV
#if EVX_ENABLE_B_FRAMES
        if (is_bidir_frame && !EVX_IS_INTRA_BLOCK_TYPE(bd->block_type))
        {
            coder->decode_ctx(2, input, feed_stream, &meta->pred_mode_ctx);
            feed_stream->read_bits(&bd->prediction_mode, 2);

            if (bd->prediction_mode == 2)
            {
                uint8 bit_count = log2((uint8) EVX_REFERENCE_FRAME_COUNT);
                coder->decode_ctx(bit_count, input, feed_stream, &meta->pred_target_b_ctx);
                feed_stream->read_bits(&bd->prediction_target_b, bit_count);

                int16 mvd_x_b = 0;
                entropy_stream_decode_value_ctx(input, coder, &meta->mvd_x_b_ctx, feed_stream, &mvd_x_b);
                bd->motion_x_b = mvd_x_b;

                int16 mvd_y_b = 0;
                entropy_stream_decode_value_ctx(input, coder, &meta->mvd_y_b_ctx, feed_stream, &mvd_y_b);
                bd->motion_y_b = mvd_y_b;

                coder->decode_ctx(1, input, feed_stream, &meta->sp_pred_b_ctx);
                feed_stream->read_bit(&bd->sp_pred_b);

                if (bd->sp_pred_b)
                {
                    coder->decode_ctx(1, input, feed_stream, &meta->sp_amount_b_ctx);
                    feed_stream->read_bit(&bd->sp_amount_b);

                    coder->decode_ctx(3, input, feed_stream, &meta->sp_index_b_ctx);
                    feed_stream->read_bits(&bd->sp_index_b, 3);
                }
            }
        }
#endif

        // QP delta for non-copy blocks
        if (!EVX_IS_COPY_BLOCK_TYPE(bd->block_type))
        {
            int16 q_delta = 0;
            entropy_stream_decode_value_ctx(input, coder, &meta->quality_ctx, feed_stream, &q_delta);
            bd->q_index = q_delta + last_q;
            last_q = bd->q_index;
        }
    }

    return EVX_SUCCESS;
}

evx_status unserialize_block_table(uint32 block_count, uint32 width_in_blocks, bit_stream *input,
    bit_stream *feed_stream, entropy_coder *coder, evx_metadata_contexts *meta,
#if EVX_ENABLE_TEMPORAL_MVP
    const evx_block_desc *prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
    bool is_bidir_frame,
#endif
    evx_block_desc *block_table)
{
    return unserialize_block_table_unified(block_count, width_in_blocks, input, feed_stream, coder, meta,
#if EVX_ENABLE_TEMPORAL_MVP
        prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
        is_bidir_frame,
#endif
        block_table);
}

#else // !EVX_ENABLE_UNIFIED_METADATA

evx_status unserialize_block_types(uint32 block_count, bit_stream *input, bit_stream *feed_stream, entropy_coder *coder, evx_block_desc *block_table)
{
    feed_stream->empty();

    for (uint32 i = 0; i < block_count; i++)
    {
        coder->decode(3, input, feed_stream, false);
        feed_stream->read_bits(&block_table[i].block_type, 3);
    }

    return EVX_SUCCESS;
}

evx_status unserialize_intra_modes(uint32 block_count, uint32 width_in_blocks, bit_stream *input, bit_stream *feed_stream, entropy_coder *coder, evx_block_desc *block_table)
{
    feed_stream->empty();

    for (uint32 i = 0; i < block_count; i++)
    {
        if (block_table[i].block_type != EVX_BLOCK_INTRA_DEFAULT)
        {
            continue;
        }

#if EVX_ENABLE_MPM
        uint8 mpm[2];
        compute_intra_mpm(block_table, width_in_blocks, i, mpm);
        bool mpm_hit = false;
        coder->decode(1, input, feed_stream, false);
        feed_stream->read_bit(&mpm_hit);
        if (mpm_hit)
        {
            bool mpm_idx = false;
            coder->decode(1, input, feed_stream, false);
            feed_stream->read_bit(&mpm_idx);
            block_table[i].intra_mode = mpm_idx ? mpm[1] : mpm[0];
        }
        else
        {
            coder->decode(3, input, feed_stream, false);
            feed_stream->read_bits(&block_table[i].intra_mode, 3);
        }
#else
        coder->decode(3, input, feed_stream, false);
        feed_stream->read_bits(&block_table[i].intra_mode, 3);
#endif
    }

    return EVX_SUCCESS;
}

evx_status unserialize_prediction_targets(uint32 block_count, bit_stream *input, bit_stream *feed_stream, entropy_coder *coder, evx_block_desc *block_table)
{
    feed_stream->empty();

    for (uint32 i = 0; i < block_count; i++)
    {
        if (EVX_IS_INTRA_BLOCK_TYPE(block_table[i].block_type))
        {
            continue;
        }

        uint8 bit_count = log2((uint8) EVX_REFERENCE_FRAME_COUNT);
        coder->decode(bit_count, input, feed_stream, false);
        feed_stream->read_bits(&block_table[i].prediction_target, bit_count);
    }

    return EVX_SUCCESS;
}

evx_status unserialize_motion_vectors(uint32 block_count, uint32 width_in_blocks,
#if EVX_ENABLE_TEMPORAL_MVP
    const evx_block_desc *prev_block_table,
#endif
    bit_stream *input, bit_stream *feed_stream, entropy_coder *coder, evx_block_desc *block_table)
{
    feed_stream->empty();

    // First pass: decode all motion_x values
    for (uint32 i = 0; i < block_count; i++)
    {
        if (!EVX_IS_MOTION_BLOCK_TYPE(block_table[i].block_type))
        {
            continue;
        }

        int16 pred_x, pred_y;
        compute_mv_predictor(block_table, width_in_blocks, i,
#if EVX_ENABLE_TEMPORAL_MVP
                              prev_block_table,
#endif
                              &pred_x, &pred_y);
        int16 mvd_x = 0;
        entropy_stream_decode_value(input, coder, feed_stream, &mvd_x);
        block_table[i].motion_x = mvd_x + pred_x;
    }

    // Second pass: decode all motion_y values
    for (uint32 i = 0; i < block_count; i++)
    {
        if (!EVX_IS_MOTION_BLOCK_TYPE(block_table[i].block_type))
        {
            continue;
        }

        int16 pred_x, pred_y;
        compute_mv_predictor(block_table, width_in_blocks, i,
#if EVX_ENABLE_TEMPORAL_MVP
                              prev_block_table,
#endif
                              &pred_x, &pred_y);
        int16 mvd_y = 0;
        entropy_stream_decode_value(input, coder, feed_stream, &mvd_y);
        block_table[i].motion_y = mvd_y + pred_y;
    }

    return EVX_SUCCESS;
}

evx_status unserialize_subpixel_motion_params(uint32 block_count, bit_stream *input, bit_stream *feed_stream, entropy_coder *coder, evx_block_desc *block_table)
{
    feed_stream->empty();

    // Subpixel prediction enabled bit
    for (uint32 i = 0; i < block_count; i++)
    {
        if (!EVX_IS_MOTION_BLOCK_TYPE(block_table[i].block_type))
        {
            continue;
        }

        coder->decode(1, input, feed_stream, false);
        feed_stream->read_bit(&block_table[i].sp_pred);
    }

    // Subpixel level bit.
    for (uint32 i = 0; i < block_count; i++)
    {
        if (!EVX_IS_MOTION_BLOCK_TYPE(block_table[i].block_type) ||
            !block_table[i].sp_pred)
        {
            continue;
        }

        coder->decode(1, input, feed_stream, false);
        feed_stream->read_bit(&block_table[i].sp_amount);
    }

    // Subpixel direction (degree) bits.
    for (uint32 i = 0; i < block_count; i++)
    {
        if (!EVX_IS_MOTION_BLOCK_TYPE(block_table[i].block_type) ||
            !block_table[i].sp_pred)
        {
            continue;
        }

        coder->decode(3, input, feed_stream, false);
        feed_stream->read_bits(&block_table[i].sp_index, 3);
    }

    return EVX_SUCCESS;
}

evx_status unserialize_block_quality(uint32 block_count, bit_stream *input, bit_stream *feed_stream, entropy_coder *coder, evx_block_desc *block_table)
{
    int16 last_q = 0;
    feed_stream->empty();

    for (uint32 i = 0; i < block_count; i++)
    {
        if (EVX_IS_COPY_BLOCK_TYPE(block_table[i].block_type))
        {
            continue;
        }

        int16 current_q = 0;
        entropy_stream_decode_value(input, coder, feed_stream, &current_q);
        block_table[i].q_index = current_q + last_q;
        last_q = block_table[i].q_index;
    }

    return EVX_SUCCESS;
}

evx_status unserialize_block_table(uint32 block_count, uint32 width_in_blocks,
#if EVX_ENABLE_TEMPORAL_MVP
    const evx_block_desc *prev_block_table,
#endif
    bit_stream *input, bit_stream *feed_stream, entropy_coder *coder, evx_block_desc *block_table)
{
    if (evx_failed(unserialize_block_types(block_count, input, feed_stream, coder, block_table)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(unserialize_intra_modes(block_count, width_in_blocks, input, feed_stream, coder, block_table)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(unserialize_prediction_targets(block_count, input, feed_stream, coder, block_table)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(unserialize_motion_vectors(block_count, width_in_blocks,
#if EVX_ENABLE_TEMPORAL_MVP
        prev_block_table,
#endif
        input, feed_stream, coder, block_table)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(unserialize_subpixel_motion_params(block_count, input, feed_stream, coder, block_table)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(unserialize_block_quality(block_count, input, feed_stream, coder, block_table)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return EVX_SUCCESS;
}

#endif // EVX_ENABLE_UNIFIED_METADATA

#if EVX_ENABLE_SAO

evx_status unserialize_sao_table(evx_sao_info *sao_table, uint32 block_count, uint32 width_in_blocks,
    bit_stream *input, bit_stream *feed_stream, entropy_coder *coder, evx_metadata_contexts *meta)
{
    for (uint32 i = 0; i < block_count; i++)
    {
        evx_sao_info *info = &sao_table[i];
        clear_sao_info(info);

        uint32 bx = i % width_in_blocks;
        uint32 by = i / width_in_blocks;

        if (bx > 0)
        {
            coder->decode_ctx(1, input, feed_stream, &meta->sao_merge_ctx);
            feed_stream->read_bit(&info->merge_left);
        }

        if (!info->merge_left && by > 0)
        {
            coder->decode_ctx(1, input, feed_stream, &meta->sao_merge_ctx);
            feed_stream->read_bit(&info->merge_above);
        }

        if (info->merge_left || info->merge_above) continue;

        evx_sao_params *components[2] = { &info->luma, &info->chroma };
        for (int c = 0; c < 2; c++)
        {
            evx_sao_params *p = components[c];

            coder->decode_ctx(2, input, feed_stream, &meta->sao_type_ctx);
            feed_stream->read_bits(&p->type, 2);

            if (p->type == EVX_SAO_EDGE)
            {
                coder->decode_ctx(2, input, feed_stream, &meta->sao_eo_class_ctx);
                feed_stream->read_bits(&p->eo_class, 2);
            }
            else if (p->type == EVX_SAO_BAND)
            {
                int16 bp = 0;
                entropy_stream_decode_value_ctx(input, coder, &meta->sao_band_pos_ctx, feed_stream, &bp);
                p->band_position = (uint8)bp;
            }

            if (p->type != EVX_SAO_OFF)
            {
                for (int k = 0; k < EVX_SAO_NUM_OFFSETS; k++)
                {
                    int16 off = 0;
                    entropy_stream_decode_value_ctx(input, coder, &meta->sao_offset_ctx, feed_stream, &off);
                    p->offsets[k] = (int8)off;
                }
            }
        }
    }

    return EVX_SUCCESS;
}

#endif // EVX_ENABLE_SAO

evx_status unserialize_slice(bit_stream *input, const evx_frame &frame, evx_context *context)
{
    uint32 block_count = context->width_in_blocks * context->height_in_blocks;

    context->arith_coder.clear();
    context->arith_coder.start_decode(input);
    for (int i = 0; i < EVX_NUM_COEFF_CONTEXTS; i++) context->coeff_contexts[i].clear();
#if EVX_ENABLE_UNIFIED_METADATA
    context->meta_contexts.clear();
#endif

    // Unserialize the encoded contents of our context, starting with the block table.
#if EVX_ENABLE_B_FRAMES
    bool is_bidir_frame = (frame.type == EVX_FRAME_BIDIR);
#endif

#if EVX_ENABLE_UNIFIED_METADATA
    if (evx_failed(unserialize_block_table(block_count, context->width_in_blocks, input,
        &context->feed_stream, &context->arith_coder, &context->meta_contexts,
#if EVX_ENABLE_TEMPORAL_MVP
        context->prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
        is_bidir_frame,
#endif
        context->block_table)))
#else
    if (evx_failed(unserialize_block_table(block_count, context->width_in_blocks,
#if EVX_ENABLE_TEMPORAL_MVP
        context->prev_block_table,
#endif
        input, &context->feed_stream, &context->arith_coder, context->block_table)))
#endif
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_SAO
    if (evx_failed(unserialize_sao_table(context->sao_table, block_count, context->width_in_blocks,
        input, &context->feed_stream, &context->arith_coder, &context->meta_contexts)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
#endif

    // Unserialize all transformed and quantized residuals.
    if (evx_failed(unserialize_macroblocks(input, context)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    // Advance read index to next byte boundary to match the encoder's byte-alignment padding.
    uint32 remainder = input->query_read_index() & 7;
    if (remainder != 0)
    {
        uint32 pad_bits = 8 - remainder;
        input->seek(pad_bits);
    }

    return EVX_SUCCESS;
}

} // namespace evx