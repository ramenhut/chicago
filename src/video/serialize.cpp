
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

evx_status serialize_block_8x8(int16 *source, uint32 source_width, int16 last_dc, int16 *cache, const uint8 *scan_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output)
{
    // Our entropy stream encode uses a scan pattern to efficiently encode our residuals.
    // This requires a contiguous input buffer, so we copy from a non-contiguous source
    // into a contiguous cache.
    for (uint32 j = 0; j < 8; j++)
    {
        aligned_byte_copy(source + j * source_width, sizeof(int16) * 8, cache + j * 8);
    }

    cache[0] = cache[0] - last_dc;  // Compute and encode a delta value for the dc.

#if EVX_ENABLE_SIGMAP_CODING
    return entropy_sigmap_encode_8x8_ctx(cache, scan_table, feed_stream, coder, contexts, output);
#else
    return entropy_rle_stream_encode_8x8_ctx(cache, scan_table, feed_stream, coder, contexts, output);
#endif
}

evx_status serialize_block_16x16(int16 *source, uint32 source_width, int16 last_dc, int16 *cache, const uint8 *scan_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output)
{
    serialize_block_8x8(source, source_width, last_dc, cache, scan_table, feed_stream, coder, contexts, output);
    serialize_block_8x8(source + 8, source_width, source[0], cache, scan_table, feed_stream, coder, contexts, output);
    serialize_block_8x8(source + 8 * source_width, source_width, source[0], cache, scan_table, feed_stream, coder, contexts, output);
    serialize_block_8x8(source + 8 * source_width + 8, source_width, source[8 * source_width], cache, scan_table, feed_stream, coder, contexts, output);

    return EVX_SUCCESS;
}

evx_status serialize_image_blocks_16x16(image *source_image, int16 *cache_data, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output)
{
    uint32 block_index = 0;
    uint32 width = source_image->query_width();
    uint32 height = source_image->query_height();

    feed_stream->empty();

    for (int32 j = 0; j < height; j += EVX_MACROBLOCK_SIZE)
    for (int32 i = 0; i < width; i += EVX_MACROBLOCK_SIZE)
    {
        evx_block_desc *block_desc = &block_table[block_index++];
        
        int16 last_dc = 0;
        int16 *last_block_data = NULL;
        int16 *block_data = reinterpret_cast<int16 *>(source_image->query_data() + source_image->query_block_offset(i, j));

        // Copy blocks contain no residuals.
        if (EVX_IS_COPY_BLOCK_TYPE(block_desc->block_type))
        {
            continue;
        }

        // Support delta dc coding
        if (i >= EVX_MACROBLOCK_SIZE)
        {
            last_block_data = reinterpret_cast<int16 *>(source_image->query_data() + source_image->query_block_offset(i - (EVX_MACROBLOCK_SIZE >> 1), j));
            last_dc = last_block_data[0];
        }
        else
        {
            if (j >= EVX_MACROBLOCK_SIZE)
            {
                // i is zero, so we sample from the block above.
                last_block_data = reinterpret_cast<int16 *>(source_image->query_data() + source_image->query_block_offset(i, j - (EVX_MACROBLOCK_SIZE >> 1)));
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
        serialize_block_16x16(block_data, width, last_dc, cache_data, scan_table, feed_stream, coder, contexts + ctx_offset, output);
    }

    return EVX_SUCCESS;
}

evx_status serialize_image_blocks_8x8(image *source_image, int16 *cache_data, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output)
{
    uint32 block_index = 0;
    uint32 width = source_image->query_width();
    uint32 height = source_image->query_height();

    feed_stream->empty();

    for (int32 j = 0; j < height; j += (EVX_MACROBLOCK_SIZE >> 1))
    for (int32 i = 0; i < width; i += (EVX_MACROBLOCK_SIZE >> 1))
    {
        evx_block_desc *block_desc = &block_table[block_index++];
        
        int16 last_dc = 0;
        int16 *last_block_data = NULL;
        int16 *block_data = reinterpret_cast<int16 *>(source_image->query_data() + source_image->query_block_offset(i, j));

        // Copy blocks contain no residuals.
        if (EVX_IS_COPY_BLOCK_TYPE(block_desc->block_type))
        {
            continue;
        }

        // Support delta dc coding
        if (i >= (EVX_MACROBLOCK_SIZE >> 1))
        {
            last_block_data = reinterpret_cast<int16 *>(source_image->query_data() + source_image->query_block_offset(i - (EVX_MACROBLOCK_SIZE >> 1), j));
            last_dc = last_block_data[0];
        }
        else
        {
            if (j >= (EVX_MACROBLOCK_SIZE >> 1))
            {
                // i is zero, so we sample from the block above.
                last_block_data = reinterpret_cast<int16 *>(source_image->query_data() + source_image->query_block_offset(i, j - (EVX_MACROBLOCK_SIZE >> 1)));
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
        serialize_block_8x8(block_data, width, last_dc, cache_data, scan_table, feed_stream, coder, contexts + ctx_offset, output);
    }

    return EVX_SUCCESS;
}

evx_status serialize_macroblocks(evx_context *context, bit_stream *output)
{
    image *y_image = context->cache_bank.output_cache.query_y_image();
    image *u_image = context->cache_bank.output_cache.query_u_image();
    image *v_image = context->cache_bank.output_cache.query_v_image();

    if (evx_failed(serialize_image_blocks_16x16(y_image, context->cache_bank.staging_block.data_y,
                   context->block_table, &context->feed_stream, &context->arith_coder, context->coeff_contexts, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_CHROMA_SUPPORT

    if (evx_failed(serialize_image_blocks_8x8(u_image, context->cache_bank.staging_block.data_u,
                   context->block_table, &context->feed_stream, &context->arith_coder, context->coeff_contexts, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(serialize_image_blocks_8x8(v_image, context->cache_bank.staging_block.data_v,
                   context->block_table, &context->feed_stream, &context->arith_coder, context->coeff_contexts, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#endif

    return EVX_SUCCESS;
}

#if EVX_ENABLE_UNIFIED_METADATA

evx_status serialize_block_table_unified(uint32 block_count, uint32 width_in_blocks, evx_block_desc *block_table,
    bit_stream *feed_stream, entropy_coder *coder, evx_metadata_contexts *meta,
#if EVX_ENABLE_TEMPORAL_MVP
    const evx_block_desc *prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
    bool is_bidir_frame,
#endif
    bit_stream *output)
{
    int16 last_q = 0;

    for (uint32 i = 0; i < block_count; i++)
    {
        evx_block_desc *bd = &block_table[i];

        // Block type: 3 bits via block_type_ctx
        feed_stream->write_bits(&bd->block_type, 3);
        coder->encode_ctx(feed_stream, output, &meta->block_type_ctx);

        // Intra mode for INTRA_DEFAULT blocks
        if (bd->block_type == EVX_BLOCK_INTRA_DEFAULT)
        {
#if EVX_ENABLE_MPM
            uint8 mpm[2];
            compute_intra_mpm(block_table, width_in_blocks, i, mpm);
            bool mpm_hit = (bd->intra_mode == mpm[0] || bd->intra_mode == mpm[1]);
            feed_stream->write_bit(mpm_hit);
            coder->encode_ctx(feed_stream, output, &meta->mpm_flag_ctx);
            if (mpm_hit)
            {
                bool mpm_idx = (bd->intra_mode == mpm[1]);
                feed_stream->write_bit(mpm_idx);
                coder->encode_ctx(feed_stream, output, &meta->mpm_idx_ctx);
            }
            else
            {
                feed_stream->write_bits(&bd->intra_mode, 3);
                coder->encode_ctx(feed_stream, output, &meta->intra_mode_ctx);
            }
#else
            feed_stream->write_bits(&bd->intra_mode, 3);
            coder->encode_ctx(feed_stream, output, &meta->intra_mode_ctx);
#endif
        }

        // Prediction target for inter blocks
        if (!EVX_IS_INTRA_BLOCK_TYPE(bd->block_type))
        {
            uint8 bit_count = log2((uint8) EVX_REFERENCE_FRAME_COUNT);
            feed_stream->write_bits(&bd->prediction_target, bit_count);
            coder->encode_ctx(feed_stream, output, &meta->pred_target_ctx);
        }

        // Motion vectors: encode X and Y together using per-component contexts.
        // With per-block encoding, the left neighbor has both motion_x AND motion_y
        // populated, producing better MV predictions.
        if (EVX_IS_MOTION_BLOCK_TYPE(bd->block_type))
        {
            int16 pred_x, pred_y;
            compute_mv_predictor(block_table, width_in_blocks, i,
#if EVX_ENABLE_TEMPORAL_MVP
                                  prev_block_table,
#endif
                                  &pred_x, &pred_y);

            int16 mvd_x = bd->motion_x - pred_x;
            entropy_stream_encode_value_ctx(mvd_x, feed_stream, coder, &meta->mvd_x_ctx, output);

            int16 mvd_y = bd->motion_y - pred_y;
            entropy_stream_encode_value_ctx(mvd_y, feed_stream, coder, &meta->mvd_y_ctx, output);

            // Subpixel prediction enabled bit
            feed_stream->write_bit(bd->sp_pred);
            coder->encode_ctx(feed_stream, output, &meta->sp_pred_ctx);

            if (bd->sp_pred)
            {
                feed_stream->write_bit(bd->sp_amount);
                coder->encode_ctx(feed_stream, output, &meta->sp_amount_ctx);

                feed_stream->write_bits(&bd->sp_index, 3);
                coder->encode_ctx(feed_stream, output, &meta->sp_index_ctx);
            }
        }

        // B-frame prediction mode and secondary reference/MV
#if EVX_ENABLE_B_FRAMES
        if (is_bidir_frame && !EVX_IS_INTRA_BLOCK_TYPE(bd->block_type))
        {
            feed_stream->write_bits(&bd->prediction_mode, 2);
            coder->encode_ctx(feed_stream, output, &meta->pred_mode_ctx);

            if (bd->prediction_mode == 2)
            {
                uint8 bit_count = log2((uint8) EVX_REFERENCE_FRAME_COUNT);
                feed_stream->write_bits(&bd->prediction_target_b, bit_count);
                coder->encode_ctx(feed_stream, output, &meta->pred_target_b_ctx);

                int16 mvd_x_b = bd->motion_x_b;
                entropy_stream_encode_value_ctx(mvd_x_b, feed_stream, coder, &meta->mvd_x_b_ctx, output);

                int16 mvd_y_b = bd->motion_y_b;
                entropy_stream_encode_value_ctx(mvd_y_b, feed_stream, coder, &meta->mvd_y_b_ctx, output);

                feed_stream->write_bit(bd->sp_pred_b);
                coder->encode_ctx(feed_stream, output, &meta->sp_pred_b_ctx);

                if (bd->sp_pred_b)
                {
                    feed_stream->write_bit(bd->sp_amount_b);
                    coder->encode_ctx(feed_stream, output, &meta->sp_amount_b_ctx);

                    feed_stream->write_bits(&bd->sp_index_b, 3);
                    coder->encode_ctx(feed_stream, output, &meta->sp_index_b_ctx);
                }
            }
        }
#endif

        // QP delta for non-copy blocks
        if (!EVX_IS_COPY_BLOCK_TYPE(bd->block_type))
        {
            int16 q_delta = bd->q_index - last_q;
            entropy_stream_encode_value_ctx(q_delta, feed_stream, coder, &meta->quality_ctx, output);
            last_q = bd->q_index;
        }
    }

    return EVX_SUCCESS;
}

evx_status serialize_block_table(uint32 block_count, uint32 width_in_blocks, evx_block_desc *block_table,
    bit_stream *feed_stream, entropy_coder *coder, evx_metadata_contexts *meta,
#if EVX_ENABLE_TEMPORAL_MVP
    const evx_block_desc *prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
    bool is_bidir_frame,
#endif
    bit_stream *output)
{
    return serialize_block_table_unified(block_count, width_in_blocks, block_table, feed_stream, coder, meta,
#if EVX_ENABLE_TEMPORAL_MVP
        prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
        is_bidir_frame,
#endif
        output);
}

#else // !EVX_ENABLE_UNIFIED_METADATA

evx_status serialize_block_types(uint32 block_count, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    feed_stream->empty();

    for (uint32 i = 0; i < block_count; i++)
    {
        feed_stream->write_bits(&block_table[i].block_type, 3);
    }

    return coder->encode(feed_stream, output, false);
}

evx_status serialize_intra_modes(uint32 block_count, uint32 width_in_blocks, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
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
        bool mpm_hit = (block_table[i].intra_mode == mpm[0] || block_table[i].intra_mode == mpm[1]);
        feed_stream->write_bit(mpm_hit);
        if (mpm_hit)
        {
            bool mpm_idx = (block_table[i].intra_mode == mpm[1]);
            feed_stream->write_bit(mpm_idx);
        }
        else
        {
            feed_stream->write_bits(&block_table[i].intra_mode, 3);
        }
#else
        feed_stream->write_bits(&block_table[i].intra_mode, 3);
#endif
    }

    return coder->encode(feed_stream, output, false);
}

evx_status serialize_prediction_targets(uint32 block_count, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    feed_stream->empty();

    for (uint32 i = 0; i < block_count; i++)
    {
        if (EVX_IS_INTRA_BLOCK_TYPE(block_table[i].block_type))
        {
            continue;
        }

        uint8 bit_count = log2((uint8) EVX_REFERENCE_FRAME_COUNT);
        feed_stream->write_bits(&block_table[i].prediction_target, bit_count);
    }

    return coder->encode(feed_stream, output, false);
}

evx_status serialize_motion_vectors(uint32 block_count, uint32 width_in_blocks, evx_block_desc *block_table,
#if EVX_ENABLE_TEMPORAL_MVP
    const evx_block_desc *prev_block_table,
#endif
    bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    // We encode motion vectors as differences from spatial median predictor.
    feed_stream->empty();

    // Encode our x component differences.
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
        int16 mvd_x = block_table[i].motion_x - pred_x;
        entropy_stream_encode_value(mvd_x, feed_stream, coder, output);
    }

    // Encode our y component differences.
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
        int16 mvd_y = block_table[i].motion_y - pred_y;
        entropy_stream_encode_value(mvd_y, feed_stream, coder, output);
    }

    return EVX_SUCCESS;
}

evx_status serialize_subpixel_motion_params(uint32 block_count, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    feed_stream->empty();

    // Subpixel prediction enabled bit
    for (uint32 i = 0; i < block_count; i++)
    {
        if (!EVX_IS_MOTION_BLOCK_TYPE(block_table[i].block_type))
        {
            continue;
        }

        feed_stream->write_bit(block_table[i].sp_pred);
        coder->encode(feed_stream, output, false);
    }

    // Subpixel level bit.
    for (uint32 i = 0; i < block_count; i++)
    {
        // The subpixel direction and level are only emitted
        // if subpixel prediction is enabled.
        if (!EVX_IS_MOTION_BLOCK_TYPE(block_table[i].block_type) ||
            !block_table[i].sp_pred)
        {
            continue;
        }

        feed_stream->write_bit(block_table[i].sp_amount);
        coder->encode(feed_stream, output, false);
    }

    // Subpixel direction (degree) bits.
    for (uint32 i = 0; i < block_count; i++)
    {
        if (!EVX_IS_MOTION_BLOCK_TYPE(block_table[i].block_type) ||
            !block_table[i].sp_pred)
        {
            continue;
        }

        feed_stream->write_bits(&block_table[i].sp_index, 3);
        coder->encode(feed_stream, output, false);
    }

    return EVX_SUCCESS;
}

evx_status serialize_block_quality(uint32 block_count, evx_block_desc *block_table, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    int16 last_q = 0;
    feed_stream->empty();

    for (uint32 i = 0; i < block_count; i++)
    {
        if (EVX_IS_COPY_BLOCK_TYPE(block_table[i].block_type))
        {
            continue;
        }

        int16 current_q = block_table[i].q_index - last_q;
        stream_encode_value(current_q, feed_stream);
        last_q = block_table[i].q_index;
    }

    return coder->encode(feed_stream, output, false);
}

evx_status serialize_block_table(uint32 block_count, uint32 width_in_blocks, evx_block_desc *block_table,
#if EVX_ENABLE_TEMPORAL_MVP
    const evx_block_desc *prev_block_table,
#endif
    bit_stream *feed_stream, entropy_coder *coder, bit_stream *output)
{
    // Descriptors are serialized contiguously to improve efficiency.
    if (evx_failed(serialize_block_types(block_count, block_table, feed_stream, coder, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(serialize_intra_modes(block_count, width_in_blocks, block_table, feed_stream, coder, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(serialize_prediction_targets(block_count, block_table, feed_stream, coder, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(serialize_motion_vectors(block_count, width_in_blocks, block_table,
#if EVX_ENABLE_TEMPORAL_MVP
        prev_block_table,
#endif
        feed_stream, coder, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(serialize_subpixel_motion_params(block_count, block_table, feed_stream, coder, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(serialize_block_quality(block_count, block_table, feed_stream, coder, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return EVX_SUCCESS;
}

#endif // EVX_ENABLE_UNIFIED_METADATA

#if EVX_ENABLE_SAO

evx_status serialize_sao_table(const evx_sao_info *sao_table, uint32 block_count, uint32 width_in_blocks,
    bit_stream *feed_stream, entropy_coder *coder, evx_metadata_contexts *meta, bit_stream *output)
{
    for (uint32 i = 0; i < block_count; i++)
    {
        const evx_sao_info *info = &sao_table[i];
        uint32 bx = i % width_in_blocks;
        uint32 by = i / width_in_blocks;

        if (bx > 0)
        {
            feed_stream->write_bit(info->merge_left);
            coder->encode_ctx(feed_stream, output, &meta->sao_merge_ctx);
        }

        if (!info->merge_left && by > 0)
        {
            feed_stream->write_bit(info->merge_above);
            coder->encode_ctx(feed_stream, output, &meta->sao_merge_ctx);
        }

        if (info->merge_left || info->merge_above) continue;

        const evx_sao_params *components[2] = { &info->luma, &info->chroma };
        for (int c = 0; c < 2; c++)
        {
            const evx_sao_params *p = components[c];

            uint8 type_val = p->type;
            feed_stream->write_bits(&type_val, 2);
            coder->encode_ctx(feed_stream, output, &meta->sao_type_ctx);

            if (p->type == EVX_SAO_EDGE)
            {
                uint8 eo_val = p->eo_class;
                feed_stream->write_bits(&eo_val, 2);
                coder->encode_ctx(feed_stream, output, &meta->sao_eo_class_ctx);
            }
            else if (p->type == EVX_SAO_BAND)
            {
                int16 bp = (int16)p->band_position;
                entropy_stream_encode_value_ctx(bp, feed_stream, coder, &meta->sao_band_pos_ctx, output);
            }

            if (p->type != EVX_SAO_OFF)
            {
                for (int k = 0; k < EVX_SAO_NUM_OFFSETS; k++)
                {
                    int16 off = (int16)p->offsets[k];
                    entropy_stream_encode_value_ctx(off, feed_stream, coder, &meta->sao_offset_ctx, output);
                }
            }
        }
    }

    return EVX_SUCCESS;
}

#endif // EVX_ENABLE_SAO

evx_status serialize_slice(const evx_frame &frame, evx_context *context, bit_stream *output)
{
    uint32 block_count = context->width_in_blocks * context->height_in_blocks;

    context->arith_coder.clear();
    for (int i = 0; i < EVX_NUM_COEFF_CONTEXTS; i++) context->coeff_contexts[i].clear();
#if EVX_ENABLE_UNIFIED_METADATA
    context->meta_contexts.clear();
#endif

    // Serialize the encoded contents of our context, starting with the block table.
#if EVX_ENABLE_B_FRAMES
    bool is_bidir_frame = (frame.type == EVX_FRAME_BIDIR);
#endif

#if EVX_ENABLE_UNIFIED_METADATA
    if (evx_failed(serialize_block_table(block_count, context->width_in_blocks, context->block_table,
        &context->feed_stream, &context->arith_coder, &context->meta_contexts,
#if EVX_ENABLE_TEMPORAL_MVP
        context->prev_block_table,
#endif
#if EVX_ENABLE_B_FRAMES
        is_bidir_frame,
#endif
        output)))
#else
    if (evx_failed(serialize_block_table(block_count, context->width_in_blocks, context->block_table,
#if EVX_ENABLE_TEMPORAL_MVP
        context->prev_block_table,
#endif
        &context->feed_stream, &context->arith_coder, output)))
#endif
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_SAO
    if (evx_failed(serialize_sao_table(context->sao_table, block_count, context->width_in_blocks,
        &context->feed_stream, &context->arith_coder, &context->meta_contexts, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
#endif

    // Serialize all transformed and quantized residuals.
    if (evx_failed(serialize_macroblocks(context, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    context->arith_coder.finish_encode(output);

    // Pad output to byte boundary so frame boundaries are byte-aligned in multi-frame streams.
    uint32 remainder = output->query_write_index() & 7;
    if (remainder != 0)
    {
        uint32 pad_bits = 8 - remainder;
        for (uint32 i = 0; i < pad_bits; i++)
            output->write_bit(0);
    }

    return EVX_SUCCESS;
}

} // namespace evx