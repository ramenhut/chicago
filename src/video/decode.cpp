
#include "evx3.h"
#include "analysis.h"
#include "config.h"
#include "convert.h"
#include "evx3dec.h"
#include "evx_memory.h"
#include "intra_pred.h"
#include "motion.h"
#include "quantize.h"
#if EVX_ENABLE_SAO
#include "sao.h"
#endif

#if EVX_ENABLE_6TAP_INTERPOLATION
#include "interp.h"
#endif

#if EVX_ENABLE_WPP
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#endif

namespace evx {

evx_status unserialize_slice(bit_stream *input, const evx_frame &frame, evx_context *context);
evx_status deblock_image_filter(evx_block_desc *block_table, image_set *target_image);

evx_status decode_block(const evx_frame &frame, const evx_block_desc &block_desc, const macroblock &source_block, 
                        evx_cache_bank *cache_bank, int32 i, int32 j, macroblock *dest_block)
{
    switch (block_desc.block_type)
    {
        case EVX_BLOCK_INTRA_DEFAULT:
        {
            inverse_quantize_macroblock(block_desc.q_index, block_desc.block_type, source_block, &cache_bank->transform_block);
            if (block_desc.intra_mode != EVX_INTRA_MODE_NONE)
            {
                macroblock pred;
                create_macroblock(cache_bank->motion_cache, 0, 0, &pred);
#if EVX_ENABLE_B_FRAMES
                uint32 intra_idx = frame.dpb_slot;
#else
                uint32 intra_idx = query_prediction_index_by_offset(frame, 0);
#endif
                generate_intra_prediction(block_desc.intra_mode,
                    cache_bank->prediction_cache[intra_idx], i, j, &pred);
                inverse_transform_add_macroblock(cache_bank->transform_block, pred, dest_block);
            }
            else
            {
                inverse_transform_macroblock(cache_bank->transform_block, dest_block);
            }

        } break;

        case EVX_BLOCK_INTRA_MOTION_COPY:
        {
            macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
            uint32 intra_pred_index = frame.dpb_slot;
#else
            uint32 intra_pred_index = query_prediction_index_by_offset(frame, 0);
#endif
            create_macroblock(cache_bank->prediction_cache[intra_pred_index], i + block_desc.motion_x, j + block_desc.motion_y, &beta_block);

            if (block_desc.sp_pred)
            {
                int16 sp_i, sp_j;
                compute_motion_direction_from_frac_index(block_desc.sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                interpolate_subpixel_6tap(cache_bank->prediction_cache[intra_pred_index],
                    i + block_desc.motion_x, j + block_desc.motion_y,
                    sp_i, sp_j, block_desc.sp_amount, &cache_bank->motion_block);
#else
                create_subpixel_macroblock(&cache_bank->prediction_cache[intra_pred_index], block_desc.sp_amount, beta_block,
                                           i + block_desc.motion_x + sp_i, j + block_desc.motion_y + sp_j, &cache_bank->motion_block);
#endif

                copy_macroblock(cache_bank->motion_block, dest_block);
            }
            else
            {
                // no sub-pixel motion estimation
                copy_macroblock(beta_block, dest_block);
            }

        } break;

        case EVX_BLOCK_INTRA_MOTION_DELTA:
        {
            macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
            uint32 intra_pred_index = frame.dpb_slot;
#else
            uint32 intra_pred_index = query_prediction_index_by_offset(frame, 0);
#endif
            create_macroblock(cache_bank->prediction_cache[intra_pred_index], i + block_desc.motion_x, j + block_desc.motion_y, &beta_block);
            inverse_quantize_macroblock(block_desc.q_index, block_desc.block_type, source_block, &cache_bank->transform_block);

            if (block_desc.sp_pred)
            {
                int16 sp_i, sp_j;
                compute_motion_direction_from_frac_index(block_desc.sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                interpolate_subpixel_6tap(cache_bank->prediction_cache[intra_pred_index],
                    i + block_desc.motion_x, j + block_desc.motion_y,
                    sp_i, sp_j, block_desc.sp_amount, &cache_bank->motion_block);
#else
                create_subpixel_macroblock(&cache_bank->prediction_cache[intra_pred_index], block_desc.sp_amount, beta_block,
                                           i + block_desc.motion_x + sp_i, j + block_desc.motion_y + sp_j, &cache_bank->motion_block);
#endif

                inverse_transform_add_macroblock(cache_bank->transform_block, cache_bank->motion_block, dest_block);
            }
            else
            {
                // no sub-pixel motion estimation
                inverse_transform_add_macroblock(cache_bank->transform_block, beta_block, dest_block);
            }

        } break;

        case EVX_BLOCK_INTER_MOTION_COPY:
        {
#if EVX_ENABLE_B_FRAMES
            if (frame.type == EVX_FRAME_BIDIR && block_desc.prediction_mode == 2)
            {
                // Bi-predicted copy: average forward and backward references
                // Use scratch buffers to avoid corrupting prediction_cache via sub-pixel writes.
                macroblock fwd_block, bwd_block;

                // Forward prediction
                create_macroblock(cache_bank->prediction_cache[block_desc.prediction_target],
                    i + block_desc.motion_x, j + block_desc.motion_y, &fwd_block);
                const macroblock *fwd_ref = &fwd_block;
                if (block_desc.sp_pred)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc.sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[block_desc.prediction_target],
                        i + block_desc.motion_x, j + block_desc.motion_y,
                        sp_i, sp_j, block_desc.sp_amount, &cache_bank->motion_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[block_desc.prediction_target],
                        block_desc.sp_amount, fwd_block,
                        i + block_desc.motion_x + sp_i, j + block_desc.motion_y + sp_j, &cache_bank->motion_block);
#endif
                    fwd_ref = &cache_bank->motion_block;
                }

                // Backward prediction
                create_macroblock(cache_bank->prediction_cache[block_desc.prediction_target_b],
                    i + block_desc.motion_x_b, j + block_desc.motion_y_b, &bwd_block);
                const macroblock *bwd_ref = &bwd_block;
                if (block_desc.sp_pred_b)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc.sp_index_b, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[block_desc.prediction_target_b],
                        i + block_desc.motion_x_b, j + block_desc.motion_y_b,
                        sp_i, sp_j, block_desc.sp_amount_b, &cache_bank->staging_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[block_desc.prediction_target_b],
                        block_desc.sp_amount_b, bwd_block,
                        i + block_desc.motion_x_b + sp_i, j + block_desc.motion_y_b + sp_j, &cache_bank->staging_block);
#endif
                    bwd_ref = &cache_bank->staging_block;
                }

                // Average: (fwd + bwd + 1) >> 1 into dest_block
                for (uint32 jj = 0; jj < EVX_MACROBLOCK_SIZE; ++jj)
                for (uint32 ii = 0; ii < EVX_MACROBLOCK_SIZE; ++ii)
                {
                    int32 fv = fwd_ref->data_y[jj * fwd_ref->stride + ii];
                    int32 bv = bwd_ref->data_y[jj * bwd_ref->stride + ii];
                    dest_block->data_y[jj * dest_block->stride + ii] = (int16)((fv + bv + 1) >> 1);
                }
                uint32 chroma_size = EVX_MACROBLOCK_SIZE >> 1;
                for (uint32 jj = 0; jj < chroma_size; ++jj)
                for (uint32 ii = 0; ii < chroma_size; ++ii)
                {
                    uint32 cs = dest_block->stride >> 1;
                    uint32 fs = fwd_ref->stride >> 1;
                    uint32 bs = bwd_ref->stride >> 1;
                    dest_block->data_u[jj * cs + ii] = (int16)((fwd_ref->data_u[jj * fs + ii] + bwd_ref->data_u[jj * bs + ii] + 1) >> 1);
                    dest_block->data_v[jj * cs + ii] = (int16)((fwd_ref->data_v[jj * fs + ii] + bwd_ref->data_v[jj * bs + ii] + 1) >> 1);
                }
            }
            else
#endif // EVX_ENABLE_B_FRAMES
            {
                macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
                uint32 inter_pred_index = block_desc.prediction_target;
#else
                uint32 inter_pred_index = query_prediction_index_by_offset(frame, block_desc.prediction_target);
#endif
                create_macroblock(cache_bank->prediction_cache[inter_pred_index], i + block_desc.motion_x, j + block_desc.motion_y, &beta_block);

                if (block_desc.sp_pred)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc.sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[inter_pred_index],
                        i + block_desc.motion_x, j + block_desc.motion_y,
                        sp_i, sp_j, block_desc.sp_amount, &cache_bank->motion_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[inter_pred_index], block_desc.sp_amount, beta_block,
                                               i + block_desc.motion_x + sp_i, j + block_desc.motion_y + sp_j, &cache_bank->motion_block);
#endif

                    copy_macroblock(cache_bank->motion_block, dest_block);
                }
                else
                {
                    // no sub-pixel motion estimation
                    copy_macroblock(beta_block, dest_block);
                }
            }

        } break;

        case EVX_BLOCK_INTER_COPY:
        {
#if EVX_ENABLE_B_FRAMES
            if (frame.type == EVX_FRAME_BIDIR && block_desc.prediction_mode == 2)
            {
                // Bi-predicted copy: average forward and backward references at (i,j)
                macroblock fwd_block, bwd_block;
                create_macroblock(cache_bank->prediction_cache[block_desc.prediction_target], i, j, &fwd_block);
                create_macroblock(cache_bank->prediction_cache[block_desc.prediction_target_b], i, j, &bwd_block);

                for (uint32 jj = 0; jj < EVX_MACROBLOCK_SIZE; ++jj)
                for (uint32 ii = 0; ii < EVX_MACROBLOCK_SIZE; ++ii)
                {
                    int32 fv = fwd_block.data_y[jj * fwd_block.stride + ii];
                    int32 bv = bwd_block.data_y[jj * bwd_block.stride + ii];
                    dest_block->data_y[jj * dest_block->stride + ii] = (int16)((fv + bv + 1) >> 1);
                }
                uint32 chroma_size = EVX_MACROBLOCK_SIZE >> 1;
                for (uint32 jj = 0; jj < chroma_size; ++jj)
                for (uint32 ii = 0; ii < chroma_size; ++ii)
                {
                    uint32 cs = dest_block->stride >> 1;
                    uint32 fs = fwd_block.stride >> 1;
                    uint32 bs = bwd_block.stride >> 1;
                    dest_block->data_u[jj * cs + ii] = (int16)((fwd_block.data_u[jj * fs + ii] + bwd_block.data_u[jj * bs + ii] + 1) >> 1);
                    dest_block->data_v[jj * cs + ii] = (int16)((fwd_block.data_v[jj * fs + ii] + bwd_block.data_v[jj * bs + ii] + 1) >> 1);
                }
            }
            else
#endif // EVX_ENABLE_B_FRAMES
            {
                macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
                uint32 inter_pred_index = block_desc.prediction_target;
#else
                uint32 inter_pred_index = query_prediction_index_by_offset(frame, block_desc.prediction_target);
#endif
                create_macroblock(cache_bank->prediction_cache[inter_pred_index], i, j, &beta_block);
                copy_macroblock(beta_block, dest_block);
            }

        } break;

        case EVX_BLOCK_INTER_MOTION_DELTA:
        {
#if EVX_ENABLE_B_FRAMES
            if (frame.type == EVX_FRAME_BIDIR && block_desc.prediction_mode == 2)
            {
                // Bi-predicted with residual
                // Use scratch buffers to avoid corrupting prediction_cache via sub-pixel writes.
                macroblock fwd_block, bwd_block;

                // Forward prediction
                create_macroblock(cache_bank->prediction_cache[block_desc.prediction_target],
                    i + block_desc.motion_x, j + block_desc.motion_y, &fwd_block);
                const macroblock *fwd_ref = &fwd_block;
                if (block_desc.sp_pred)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc.sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[block_desc.prediction_target],
                        i + block_desc.motion_x, j + block_desc.motion_y,
                        sp_i, sp_j, block_desc.sp_amount, &cache_bank->motion_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[block_desc.prediction_target],
                        block_desc.sp_amount, fwd_block,
                        i + block_desc.motion_x + sp_i, j + block_desc.motion_y + sp_j, &cache_bank->motion_block);
#endif
                    fwd_ref = &cache_bank->motion_block;
                }

                // Backward prediction
                create_macroblock(cache_bank->prediction_cache[block_desc.prediction_target_b],
                    i + block_desc.motion_x_b, j + block_desc.motion_y_b, &bwd_block);
                const macroblock *bwd_ref = &bwd_block;
                if (block_desc.sp_pred_b)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc.sp_index_b, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[block_desc.prediction_target_b],
                        i + block_desc.motion_x_b, j + block_desc.motion_y_b,
                        sp_i, sp_j, block_desc.sp_amount_b, &cache_bank->transform_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[block_desc.prediction_target_b],
                        block_desc.sp_amount_b, bwd_block,
                        i + block_desc.motion_x_b + sp_i, j + block_desc.motion_y_b + sp_j, &cache_bank->transform_block);
#endif
                    bwd_ref = &cache_bank->transform_block;
                }

                // Average: (fwd + bwd + 1) >> 1 into staging_block
                for (uint32 jj = 0; jj < EVX_MACROBLOCK_SIZE; ++jj)
                for (uint32 ii = 0; ii < EVX_MACROBLOCK_SIZE; ++ii)
                {
                    int32 fv = fwd_ref->data_y[jj * fwd_ref->stride + ii];
                    int32 bv = bwd_ref->data_y[jj * bwd_ref->stride + ii];
                    cache_bank->staging_block.data_y[jj * cache_bank->staging_block.stride + ii] = (int16)((fv + bv + 1) >> 1);
                }
                uint32 chroma_size = EVX_MACROBLOCK_SIZE >> 1;
                for (uint32 jj = 0; jj < chroma_size; ++jj)
                for (uint32 ii = 0; ii < chroma_size; ++ii)
                {
                    uint32 cs = cache_bank->staging_block.stride >> 1;
                    uint32 fs = fwd_ref->stride >> 1;
                    uint32 bs = bwd_ref->stride >> 1;
                    cache_bank->staging_block.data_u[jj * cs + ii] = (int16)((fwd_ref->data_u[jj * fs + ii] + bwd_ref->data_u[jj * bs + ii] + 1) >> 1);
                    cache_bank->staging_block.data_v[jj * cs + ii] = (int16)((fwd_ref->data_v[jj * fs + ii] + bwd_ref->data_v[jj * bs + ii] + 1) >> 1);
                }

                // Add residual
                inverse_quantize_macroblock(block_desc.q_index, block_desc.block_type,
                    source_block, &cache_bank->transform_block);
                inverse_transform_add_macroblock(cache_bank->transform_block, cache_bank->staging_block, dest_block);
            }
            else
#endif // EVX_ENABLE_B_FRAMES
            {
                macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
                uint32 inter_pred_index = block_desc.prediction_target;
#else
                uint32 inter_pred_index = query_prediction_index_by_offset(frame, block_desc.prediction_target);
#endif
                create_macroblock(cache_bank->prediction_cache[inter_pred_index], i + block_desc.motion_x, j + block_desc.motion_y, &beta_block);
                inverse_quantize_macroblock(block_desc.q_index, block_desc.block_type, source_block, &cache_bank->transform_block);

                if (block_desc.sp_pred)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc.sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[inter_pred_index],
                        i + block_desc.motion_x, j + block_desc.motion_y,
                        sp_i, sp_j, block_desc.sp_amount, &cache_bank->motion_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[inter_pred_index], block_desc.sp_amount, beta_block,
                                               i + block_desc.motion_x + sp_i, j + block_desc.motion_y + sp_j, &cache_bank->motion_block);
#endif

                    inverse_transform_add_macroblock(cache_bank->transform_block, cache_bank->motion_block, dest_block);
                }
                else
                {
                    // no sub-pixel motion estimation
                    inverse_transform_add_macroblock(cache_bank->transform_block, beta_block, dest_block);
                }
            }

        } break;

        case EVX_BLOCK_INTER_DELTA:
        {
#if EVX_ENABLE_B_FRAMES
            if (frame.type == EVX_FRAME_BIDIR && block_desc.prediction_mode == 2)
            {
                // Bi-predicted with residual: average refs at (i,j) then add residual
                macroblock fwd_block, bwd_block;
                create_macroblock(cache_bank->prediction_cache[block_desc.prediction_target], i, j, &fwd_block);
                create_macroblock(cache_bank->prediction_cache[block_desc.prediction_target_b], i, j, &bwd_block);

                for (uint32 jj = 0; jj < EVX_MACROBLOCK_SIZE; ++jj)
                for (uint32 ii = 0; ii < EVX_MACROBLOCK_SIZE; ++ii)
                {
                    int32 fv = fwd_block.data_y[jj * fwd_block.stride + ii];
                    int32 bv = bwd_block.data_y[jj * bwd_block.stride + ii];
                    cache_bank->staging_block.data_y[jj * cache_bank->staging_block.stride + ii] = (int16)((fv + bv + 1) >> 1);
                }
                uint32 chroma_size = EVX_MACROBLOCK_SIZE >> 1;
                for (uint32 jj = 0; jj < chroma_size; ++jj)
                for (uint32 ii = 0; ii < chroma_size; ++ii)
                {
                    uint32 cs = cache_bank->staging_block.stride >> 1;
                    uint32 fs = fwd_block.stride >> 1;
                    uint32 bs = bwd_block.stride >> 1;
                    cache_bank->staging_block.data_u[jj * cs + ii] = (int16)((fwd_block.data_u[jj * fs + ii] + bwd_block.data_u[jj * bs + ii] + 1) >> 1);
                    cache_bank->staging_block.data_v[jj * cs + ii] = (int16)((fwd_block.data_v[jj * fs + ii] + bwd_block.data_v[jj * bs + ii] + 1) >> 1);
                }

                inverse_quantize_macroblock(block_desc.q_index, block_desc.block_type, source_block, &cache_bank->transform_block);
                inverse_transform_add_macroblock(cache_bank->transform_block, cache_bank->staging_block, dest_block);
            }
            else
#endif // EVX_ENABLE_B_FRAMES
            {
                macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
                uint32 inter_pred_index = block_desc.prediction_target;
#else
                uint32 inter_pred_index = query_prediction_index_by_offset(frame, block_desc.prediction_target);
#endif
                create_macroblock(cache_bank->prediction_cache[inter_pred_index], i, j, &beta_block);
                inverse_quantize_macroblock(block_desc.q_index, block_desc.block_type, source_block, &cache_bank->transform_block);
                inverse_transform_add_macroblock(cache_bank->transform_block, beta_block, dest_block);
            }

        } break;

        default: return evx_post_error(EVX_ERROR_INVALID_RESOURCE);;
    };

    return EVX_SUCCESS;
}

evx_status decode_slice(const evx_frame &frame, evx_context *context)
{
    uint32 block_index = 0;
    uint32 width = query_context_width(*context);
    uint32 height = query_context_height(*context);
#if EVX_ENABLE_B_FRAMES
    uint32 dest_index = frame.dpb_slot;
#else
    uint32 dest_index = query_prediction_index_by_offset(frame, 0);
#endif

    macroblock source_block, dest_block;

    for (int32 j = 0; j < height; j += EVX_MACROBLOCK_SIZE)
    for (int32 i = 0; i < width;  i += EVX_MACROBLOCK_SIZE)
    {
        evx_block_desc *block_desc = &context->block_table[block_index++];
        
        create_macroblock(context->cache_bank.input_cache, i, j, &source_block);
        create_macroblock(context->cache_bank.prediction_cache[dest_index], i, j, &dest_block);

        if (evx_failed(decode_block(frame, *block_desc, source_block, &context->cache_bank, i, j, &dest_block)))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

#if EVX_ENABLE_WPP

evx_status decode_slice_wpp(const evx_frame &frame, evx_context *context)
{
    uint32 cols = context->width_in_blocks;
    uint32 rows = context->height_in_blocks;
#if EVX_ENABLE_B_FRAMES
    uint32 dest_index = frame.dpb_slot;
#else
    uint32 dest_index = query_prediction_index_by_offset(frame, 0);
#endif

    uint32 num_threads = std::max(1u, std::min((uint32)std::thread::hardware_concurrency(), rows));
    if (num_threads < 2)
        return decode_slice(frame, context);

    std::vector<std::atomic<int32>> row_progress(rows);
    for (uint32 r = 0; r < rows; r++)
        row_progress[r].store(-1, std::memory_order_relaxed);

    std::atomic<uint32> next_row(0);
    std::atomic<bool> error_flag(false);

    auto worker = [&]() {
        evx_cache_bank thread_bank;
        thread_bank.input_cache.borrow(context->cache_bank.input_cache);
        thread_bank.output_cache.borrow(context->cache_bank.output_cache);
        for (uint32 p = 0; p < EVX_REFERENCE_FRAME_COUNT; p++)
            thread_bank.prediction_cache[p].borrow(context->cache_bank.prediction_cache[p]);

        thread_bank.transform_cache.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE);
        thread_bank.motion_cache.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE);
        thread_bank.staging_cache.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE);
        create_macroblock(thread_bank.transform_cache, 0, 0, &thread_bank.transform_block);
        create_macroblock(thread_bank.motion_cache, 0, 0, &thread_bank.motion_block);
        create_macroblock(thread_bank.staging_cache, 0, 0, &thread_bank.staging_block);

        while (!error_flag.load(std::memory_order_relaxed))
        {
            uint32 row = next_row.fetch_add(1, std::memory_order_relaxed);
            if (row >= rows) break;

            int32 py = (int32)(row * EVX_MACROBLOCK_SIZE);

            for (uint32 col = 0; col < cols; col++)
            {
                if (error_flag.load(std::memory_order_relaxed)) break;

                if (row > 0)
                {
                    // col+4 dependency: intra motion search can reach 32px right (16 initial +
                    // 15 refinement + 1 subpel), plus 16px macroblock + 3px 6-tap overshoot = 50px
                    // total, which spans into macroblock col+3.  col+4 provides safety margin.
                    int32 required = (int32)std::min(col + 4, cols - 1);
                    while (row_progress[row - 1].load(std::memory_order_acquire) < required)
                    {
                        if (error_flag.load(std::memory_order_relaxed)) return;
                        std::this_thread::yield();
                    }
                }

                int32 px = (int32)(col * EVX_MACROBLOCK_SIZE);
                uint32 block_index = row * cols + col;
                evx_block_desc *block_desc = &context->block_table[block_index];

                macroblock source_block, dest_block;
                create_macroblock(thread_bank.input_cache, px, py, &source_block);
                create_macroblock(thread_bank.prediction_cache[dest_index], px, py, &dest_block);

                if (evx_failed(decode_block(frame, *block_desc, source_block, &thread_bank, px, py, &dest_block)))
                {
                    error_flag.store(true, std::memory_order_relaxed);
                    return;
                }

                row_progress[row].store((int32)col, std::memory_order_release);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (uint32 t = 0; t < num_threads; t++)
        threads.emplace_back(worker);

    for (auto &t : threads)
        t.join();

    if (error_flag.load(std::memory_order_relaxed))
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

    return EVX_SUCCESS;
}

#endif // EVX_ENABLE_WPP

evx_status engine_decode_frame(bit_stream *input, const evx_frame &frame, evx_context *context, image *output)
{
    if (evx_failed(unserialize_slice(input, frame, context)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_B_FRAMES
    uint32 dest_index = frame.dpb_slot;
#else
    uint32 dest_index = query_prediction_index_by_offset(frame, 0);
#endif

#if EVX_ENABLE_WPP
    if (evx_failed(decode_slice_wpp(frame, context)))
#else
    if (evx_failed(decode_slice(frame, context)))
#endif
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    // Run our in-loop deblocking filter on the final post prediction image.
    if (evx_failed(deblock_image_filter(context->block_table, &context->cache_bank.prediction_cache[dest_index])))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_SAO
    if (evx_failed(sao_filter_image(context->sao_table, context->width_in_blocks, context->height_in_blocks,
        &context->cache_bank.prediction_cache[dest_index])))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
#endif

    if (evx_failed(convert_image(context->cache_bank.prediction_cache[dest_index], output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_TEMPORAL_MVP
#if EVX_ENABLE_B_FRAMES
    if (frame.type != EVX_FRAME_BIDIR)
#endif
    {
        uint32 block_count = context->width_in_blocks * context->height_in_blocks;
        aligned_byte_copy(context->block_table, sizeof(evx_block_desc) * block_count, context->prev_block_table);
    }
#endif

    return EVX_SUCCESS;
}

} // namespace evx