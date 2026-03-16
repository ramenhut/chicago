
#include "evx3.h"
#include "analysis.h"
#include "config.h"
#include "convert.h"
#include "evx3enc.h"
#include "evx_memory.h"
#include "intra_pred.h"
#include "motion.h"
#include "quantize.h"
#include "scan.h"
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

evx_status deblock_image_filter(evx_block_desc *block_table, image_set *target_image);
evx_status serialize_slice(const evx_frame &frame, evx_context *context, bit_stream *output);
evx_status decode_block(const evx_frame &frame, const evx_block_desc &block_desc, const macroblock &source_block,
                        evx_cache_bank *cache_bank, int32 i, int32 j, macroblock *dest_block);
evx_status encode_block(const evx_frame &frame, const macroblock &source_block, evx_cache_bank *cache_bank,
                        int32 i, int32 j, evx_block_desc *block_desc, macroblock *dest_block);

#if EVX_ENABLE_RDO

// Estimate the number of bits required to encode a block descriptor and its residual.
static uint32 estimate_block_bits(const evx_block_desc &desc, const macroblock &quantized_block)
{
    uint32 bits = 3; // block type

    if (EVX_IS_COPY_BLOCK_TYPE(desc.block_type))
    {
        // Copy blocks have no residual, just block type + optional metadata.
        if (!EVX_IS_INTRA_BLOCK_TYPE(desc.block_type))
            bits += 2; // prediction_target
        if (EVX_IS_MOTION_BLOCK_TYPE(desc.block_type))
        {
            // MV deltas: ~2*log2(|mv|+1) per component
            bits += 2 * (evx_required_bits((uint8)(abs(desc.motion_x) + 1))) + 2 * (evx_required_bits((uint8)(abs(desc.motion_y) + 1)));
            if (desc.sp_pred) bits += 5; // sp_pred + sp_amount + sp_index(3)
            else bits += 1;
        }
        return bits;
    }

    // Non-copy blocks: metadata + residual estimate.
    if (!EVX_IS_INTRA_BLOCK_TYPE(desc.block_type))
        bits += 2; // prediction_target

    if (EVX_IS_MOTION_BLOCK_TYPE(desc.block_type))
    {
        bits += 2 * (evx_required_bits((uint8)(abs(desc.motion_x) + 1))) + 2 * (evx_required_bits((uint8)(abs(desc.motion_y) + 1)));
        if (desc.sp_pred) bits += 5;
        else bits += 1;
    }

    // Intra mode for INTRA_DEFAULT blocks
    if (desc.block_type == EVX_BLOCK_INTRA_DEFAULT)
#if EVX_ENABLE_MPM
        bits += 3; // MPM flag (1) + index (1) on hit; flag (1) + mode (3) on miss; ~3 expected
#else
        bits += 3;
#endif

    // QP delta: ~2*log2(|qp_delta|+1)
    bits += 2 * (evx_required_bits((uint8)(abs(desc.q_index) + 1)));

    // Residual: estimate bits for each 8x8 sub-block in the 16x16 macroblock (Y plane).
    for (uint32 bj = 0; bj < EVX_MACROBLOCK_SIZE; bj += 8)
    for (uint32 bi = 0; bi < EVX_MACROBLOCK_SIZE; bi += 8)
    {
        uint32 nonzero_count = 0;
        uint32 coeff_bits = 0;
        for (uint32 y = 0; y < 8; y++)
        for (uint32 x = 0; x < 8; x++)
        {
            int16 val = quantized_block.data_y[(bj + y) * quantized_block.stride + (bi + x)];
            if (val != 0)
            {
                nonzero_count++;
#if EVX_ENABLE_SIGMAP_CODING
                // Sigmap format: 1 sig_flag + 1 gt1_flag + 1 sign + conditional gt2/remainder
                uint16 abs_val = (uint16)evx_min2(abs(val), 255);
                coeff_bits += 2; // sig_flag + sign
                if (abs_val > 1) coeff_bits += 1; // gt1
                if (abs_val > 2) coeff_bits += 1 + 2 * evx_required_bits((uint8)(abs_val - 3 + 1)); // gt2 + remainder
                else coeff_bits += 1; // gt1 flag alone
#else
                coeff_bits += 2 * evx_required_bits((uint8)evx_min2(abs(val), 255)) + 2;
#endif
            }
        }
#if EVX_ENABLE_SIGMAP_CODING
        // last_sig Golomb + sig_map bits for zero positions
        bits += evx_required_bits((uint8)(nonzero_count + 1)) * 2 + 1;
#else
        bits += evx_required_bits((uint8)(nonzero_count + 1)) * 2 + 1;
#endif
        bits += coeff_bits;
    }

    return bits;
}

// Compute the SSD (distortion) between the original source block and a reconstructed block.
static int32 compute_block_ssd_y(const macroblock &source, const macroblock &reconstructed)
{
    int32 ssd = 0;
    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; i++)
    {
        int32 diff = source.data_y[j * source.stride + i] - reconstructed.data_y[j * reconstructed.stride + i];
        ssd += diff * diff;
    }
    return ssd;
}

// Compute RDO cost for a candidate block descriptor. Speculatively encodes then decodes.
static float compute_rdo_cost(const evx_frame &frame, const macroblock &source_block,
                              evx_cache_bank *cache_bank, int32 i, int32 j,
                              evx_block_desc *candidate_desc, float lambda)
{
    if (EVX_IS_COPY_BLOCK_TYPE(candidate_desc->block_type))
    {
        // For copy blocks, reconstruct using the decode path to get SSD.
        macroblock reconstructed;
        macroblock dummy_source; // copy blocks don't use source in decode
        create_macroblock(cache_bank->staging_cache, 0, 0, &reconstructed);

        decode_block(frame, *candidate_desc, dummy_source, cache_bank, i, j, &reconstructed);

        int32 distortion = compute_block_ssd_y(source_block, reconstructed);
        uint32 rate = estimate_block_bits(*candidate_desc, dummy_source);
        return (float)distortion + lambda * (float)rate;
    }

    // For delta blocks: run encode_block to get quantized residual, then decode to reconstruct.
    macroblock quantized;
    create_macroblock(cache_bank->staging_cache, 0, 0, &quantized);

    // encode_block fills quantized and sets q_index/variance on candidate_desc.
    encode_block(frame, source_block, cache_bank, i, j, candidate_desc, &quantized);

    // Now decode to get the reconstructed block.
    macroblock reconstructed;
    create_macroblock(cache_bank->staging_cache, 0, 0, &reconstructed);
    decode_block(frame, *candidate_desc, quantized, cache_bank, i, j, &reconstructed);

    int32 distortion = compute_block_ssd_y(source_block, reconstructed);
    uint32 rate = estimate_block_bits(*candidate_desc, quantized);
    return (float)distortion + lambda * (float)rate;
}

#endif // EVX_ENABLE_RDO

evx_status classify_block(const evx_frame &frame, const macroblock &source_block, evx_cache_bank *cache_bank, int32 i, int32 j,
                          const evx_block_desc *block_table, uint32 width_in_blocks, uint32 block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                          const evx_block_desc *prev_block_table,
#endif
                          evx_block_desc *output)
{
    evx_block_desc best_desc;

    // calculate_intra_prediction will return the source block desc if there is no better intra alternative.
    int32 best_sad = calculate_intra_prediction(frame, source_block, i, j, cache_bank, &best_desc);

    if (EVX_FRAME_INTER == frame.type)
    {
#if EVX_ENABLE_B_FRAMES
        // With B-frames, I/P frames use explicit DPB slots. Only one reference is available.
        {
            evx_block_desc inter_desc;
            int32 inter_sad = perform_inter_search(frame, source_block, i, j, cache_bank,
                frame.ref_slot_fwd, block_table, width_in_blocks, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                prev_block_table,
#endif
                &inter_desc);
            inter_desc.prediction_target = frame.ref_slot_fwd;

            if (EVX_IS_COPY_BLOCK_TYPE(inter_desc.block_type) ^ EVX_IS_COPY_BLOCK_TYPE(best_desc.block_type))
            {
                if (EVX_IS_COPY_BLOCK_TYPE(inter_desc.block_type))
                {
                    best_desc = inter_desc;
                    best_sad = inter_sad;
                }
            }
            else
            {
                if (inter_sad < best_sad)
                {
                    best_desc = inter_desc;
                    best_sad = inter_sad;
                }
            }
        }
#else
        // This loop will prioritize closer predictions over far. The further the prediction index
        // the more costly it becomes to encode it, so we should require increasingly higher thresholds
        // for further predictions.
        for (uint8 offset = 1; offset < EVX_REFERENCE_FRAME_COUNT; ++offset)
        {
            evx_block_desc inter_desc;

            // calculate_inter_prediction will always return a best candidate.
            int32 inter_sad = calculate_inter_prediction(frame, source_block, i, j, cache_bank, offset,
                                                          block_table, width_in_blocks, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                                                          prev_block_table,
#endif
                                                          &inter_desc);

            // Bias toward closer references: distant refs need a larger SAD improvement.
            int32 distance_bias = (int32)(compute_lambda(frame.quality) * (float)(offset * 2));

            if (EVX_IS_COPY_BLOCK_TYPE(inter_desc.block_type) ^ EVX_IS_COPY_BLOCK_TYPE(best_desc.block_type))
            {
                // Always keep the copy block.
                if (EVX_IS_COPY_BLOCK_TYPE(inter_desc.block_type))
                {
                    best_desc = inter_desc;
                    best_sad = inter_sad;
                }
            }
            else
            {
                if (inter_sad + distance_bias < best_sad)
                {
                    best_desc = inter_desc;
                    best_sad = inter_sad;
                }
            }
        }
#endif
    }

    if (best_sad == EVX_MAX_INT32)
    {
        // Critical error during motion estimation.
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    // Evaluate directional intra prediction as a candidate.
    {
#if EVX_ENABLE_B_FRAMES
        uint32 intra_pred_index = frame.dpb_slot;
#else
        uint32 intra_pred_index = query_prediction_index_by_offset(frame, 0);
#endif
        macroblock scratch;
        create_macroblock(cache_bank->motion_cache, 0, 0, &scratch);
        int32 dir_sad = 0;
        uint8 best_intra_mode = select_best_intra_mode(source_block,
            cache_bank->prediction_cache[intra_pred_index], i, j, &scratch, &dir_sad);

        if (dir_sad < best_sad)
        {
            clear_block_desc(&best_desc);
            EVX_SET_INTRA_BLOCK_TYPE_BIT(best_desc.block_type, true);
            best_desc.intra_mode = best_intra_mode;
            best_sad = dir_sad;
        }
    }

#if EVX_ENABLE_RDO
    // RDO refinement: if we have both an intra and inter candidate, compare using J = D + λ*R.
    // For the first implementation, compare the SAD-selected winner against the intra default.
#if EVX_ENABLE_RDO_COPY_EVAL
    if (EVX_FRAME_INTER == frame.type)
#else
    if (EVX_FRAME_INTER == frame.type && !EVX_IS_COPY_BLOCK_TYPE(best_desc.block_type))
#endif
    {
        float lambda = compute_lambda(frame.quality);

        // Evaluate the SAD-selected candidate.
        evx_block_desc rdo_candidate = best_desc;
        float best_cost = compute_rdo_cost(frame, source_block, cache_bank, i, j, &rdo_candidate, lambda);
        best_desc = rdo_candidate;

        // Also evaluate intra default if the winner isn't already intra default.
        if (best_desc.block_type != EVX_BLOCK_INTRA_DEFAULT)
        {
            evx_block_desc intra_desc;
            clear_block_desc(&intra_desc);
            EVX_SET_INTRA_BLOCK_TYPE_BIT(intra_desc.block_type, true);

            float intra_cost = compute_rdo_cost(frame, source_block, cache_bank, i, j, &intra_desc, lambda);
            if (intra_cost < best_cost)
            {
                best_desc = intra_desc;
                best_cost = intra_cost;
            }
        }

#if EVX_ENABLE_RDO_INTRA_MODES
        // Evaluate all 3 gradient-pruned intra mode candidates via RDO.
        {
#if EVX_ENABLE_B_FRAMES
            uint32 rdo_pred_index = frame.dpb_slot;
#else
            uint32 rdo_pred_index = query_prediction_index_by_offset(frame, 0);
#endif
            uint8 intra_candidates[3];
            select_intra_mode_candidates(
                cache_bank->prediction_cache[rdo_pred_index], i, j, intra_candidates);

            for (int c = 0; c < 3; c++)
            {
                evx_block_desc dir_desc;
                clear_block_desc(&dir_desc);
                EVX_SET_INTRA_BLOCK_TYPE_BIT(dir_desc.block_type, true);
                dir_desc.intra_mode = intra_candidates[c];

                float dir_cost = compute_rdo_cost(frame, source_block, cache_bank, i, j, &dir_desc, lambda);
                if (dir_cost < best_cost)
                {
                    best_desc = dir_desc;
                    best_cost = dir_cost;
                }
            }
        }
#else
        // Evaluate directional intra prediction via RDO (single SAD-winner).
        if (best_desc.intra_mode == EVX_INTRA_MODE_NONE)
        {
#if EVX_ENABLE_B_FRAMES
            uint32 rdo_pred_index = frame.dpb_slot;
#else
            uint32 rdo_pred_index = query_prediction_index_by_offset(frame, 0);
#endif
            macroblock rdo_scratch;
            create_macroblock(cache_bank->motion_cache, 0, 0, &rdo_scratch);
            int32 unused_sad;
            uint8 rdo_intra_mode = select_best_intra_mode(source_block,
                cache_bank->prediction_cache[rdo_pred_index], i, j, &rdo_scratch, &unused_sad);

            evx_block_desc dir_desc;
            clear_block_desc(&dir_desc);
            EVX_SET_INTRA_BLOCK_TYPE_BIT(dir_desc.block_type, true);
            dir_desc.intra_mode = rdo_intra_mode;

            float dir_cost = compute_rdo_cost(frame, source_block, cache_bank, i, j, &dir_desc, lambda);
            if (dir_cost < best_cost)
            {
                best_desc = dir_desc;
            }
        }
#endif
    }
#endif

    *output = best_desc;

    return EVX_SUCCESS;
}

#if EVX_ENABLE_B_FRAMES

evx_status classify_block_bidir(const evx_frame &frame, const macroblock &source_block,
                                 evx_cache_bank *cache_bank, int32 i, int32 j,
                                 const evx_block_desc *block_table, uint32 width_in_blocks,
                                 uint32 block_index, evx_block_desc *output)
{
    float lambda = compute_lambda(frame.quality);

    // Candidate 1: Intra prediction (always available)
    evx_block_desc best_desc;
    int32 best_sad = calculate_intra_prediction(frame, source_block, i, j, cache_bank, &best_desc);

#if !EVX_FORCE_INTRA_BFRAMES
    // Candidate 2: Forward-only (against past reference)
#if !EVX_DISABLE_FWD_PRED
    {
        evx_block_desc fwd_desc;
        int32 fwd_sad = perform_inter_search(frame, source_block, i, j, cache_bank,
            frame.ref_slot_fwd, block_table, width_in_blocks, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
            nullptr,
#endif
            &fwd_desc);
        fwd_desc.prediction_mode = 0;
        fwd_desc.prediction_target = frame.ref_slot_fwd;

        if (fwd_sad < best_sad)
        {
            best_desc = fwd_desc;
            best_sad = fwd_sad;
        }
    }
#endif

    // Candidate 3: Backward-only (against future reference)
#if !EVX_DISABLE_BWD_PRED
    {
        evx_block_desc bwd_desc;
        int32 bwd_sad = perform_inter_search(frame, source_block, i, j, cache_bank,
            frame.ref_slot_bwd, block_table, width_in_blocks, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
            nullptr,
#endif
            &bwd_desc);
        bwd_desc.prediction_mode = 1;
        bwd_desc.prediction_target = frame.ref_slot_bwd;

        if (bwd_sad < best_sad)
        {
            best_desc = bwd_desc;
            best_sad = bwd_sad;
        }
    }
#endif

    // Candidate 4: Bi-predicted
#if !EVX_DISABLE_BIDIR_PRED
    {
        evx_block_desc bi_desc;
        int32 bi_sad = calculate_bidir_prediction(frame, source_block, i, j, cache_bank,
            frame.ref_slot_fwd, frame.ref_slot_bwd,
            block_table, width_in_blocks, block_index, &bi_desc);

        int32 bi_bias = (int32)(lambda * 8.0f);
        if (bi_sad + bi_bias < best_sad)
        {
            best_desc = bi_desc;
            best_sad = bi_sad;
        }
    }
#endif
#endif // !EVX_FORCE_INTRA_BFRAMES

    if (best_sad == EVX_MAX_INT32)
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if !EVX_FORCE_INTRA_BFRAMES
    // Directional intra prediction candidate
    {
        uint32 intra_pred_index = frame.dpb_slot;
        macroblock scratch;
        create_macroblock(cache_bank->motion_cache, 0, 0, &scratch);
        int32 dir_sad = 0;
        uint8 best_intra_mode = select_best_intra_mode(source_block,
            cache_bank->prediction_cache[intra_pred_index], i, j, &scratch, &dir_sad);

        if (dir_sad < best_sad)
        {
            clear_block_desc(&best_desc);
            best_desc.block_type = EVX_BLOCK_INTRA_DEFAULT;
            best_desc.intra_mode = best_intra_mode;
            best_desc.prediction_mode = 0;
            best_sad = dir_sad;
        }
    }
#endif

    // Skip full RDO for B-frames in initial implementation (SAD-based selection is sufficient)
    // TODO: Add RDO evaluation for B-frame candidates

    *output = best_desc;
    return EVX_SUCCESS;
}

#endif // EVX_ENABLE_B_FRAMES

evx_status encode_block(const evx_frame &frame, const macroblock &source_block, evx_cache_bank *cache_bank,
                        int32 i, int32 j, evx_block_desc *block_desc, macroblock *dest_block)
{
    // Classification only performs a fast block comparison and interpolation, so we recalculate
    // the full block values when necessary.

    switch (block_desc->block_type)
    {
        case EVX_BLOCK_INTRA_DEFAULT:
        {
            if (block_desc->intra_mode != EVX_INTRA_MODE_NONE)
            {
                macroblock pred;
                create_macroblock(cache_bank->motion_cache, 0, 0, &pred);
#if EVX_ENABLE_B_FRAMES
                uint32 intra_idx = frame.dpb_slot;
#else
                uint32 intra_idx = query_prediction_index_by_offset(frame, 0);
#endif
                generate_intra_prediction(block_desc->intra_mode,
                    cache_bank->prediction_cache[intra_idx], i, j, &pred);
                sub_transform_macroblock(source_block, pred, &cache_bank->transform_block);
            }
            else
            {
                transform_macroblock(source_block, &cache_bank->transform_block);
            }
            block_desc->q_index = query_block_quantization_parameter(frame.quality, cache_bank->transform_block, block_desc->block_type);
            block_desc->variance = compute_block_variance2(cache_bank->transform_block);
            quantize_macroblock(block_desc->q_index, block_desc->block_type, cache_bank->transform_block, dest_block);

#if EVX_ENABLE_TRELLIS_QUANTIZATION
            {
                const uint8 *scan = select_scan_table_8x8(block_desc->intra_mode);
                float lambda = compute_lambda(frame.quality);
                trellis_optimize_macroblock(block_desc->q_index, block_desc->block_type,
                    cache_bank->transform_block, scan, lambda, dest_block);
            }
#endif

        } break;

        case EVX_BLOCK_INTRA_MOTION_DELTA:
        {
            macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
            uint32 intra_pred_index = frame.dpb_slot;
#else
            uint32 intra_pred_index = query_prediction_index_by_offset(frame, 0);
#endif
            create_macroblock(cache_bank->prediction_cache[intra_pred_index], i + block_desc->motion_x, j + block_desc->motion_y, &beta_block);

            if (block_desc->sp_pred)
            {
                int16 sp_i, sp_j;
                compute_motion_direction_from_frac_index(block_desc->sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                interpolate_subpixel_6tap(cache_bank->prediction_cache[intra_pred_index],
                    i + block_desc->motion_x, j + block_desc->motion_y,
                    sp_i, sp_j, block_desc->sp_amount, &cache_bank->motion_block);
#else
                create_subpixel_macroblock(&cache_bank->prediction_cache[intra_pred_index], block_desc->sp_amount, beta_block,
                                           i + block_desc->motion_x + sp_i, j + block_desc->motion_y + sp_j, &cache_bank->motion_block);
#endif

                sub_transform_macroblock(source_block, cache_bank->motion_block, &cache_bank->transform_block);
            }
            else
            {
                // no sub-pixel motion estimation
                sub_transform_macroblock(source_block, beta_block, &cache_bank->transform_block);
            }

            block_desc->q_index = query_block_quantization_parameter(frame.quality, cache_bank->transform_block, block_desc->block_type);
            block_desc->variance = compute_block_variance2(cache_bank->transform_block);
            quantize_macroblock(block_desc->q_index, block_desc->block_type, cache_bank->transform_block, dest_block);

#if EVX_ENABLE_TRELLIS_QUANTIZATION
            {
                const uint8 *scan = select_scan_table_8x8(block_desc->intra_mode);
                float lambda = compute_lambda(frame.quality);
                trellis_optimize_macroblock(block_desc->q_index, block_desc->block_type,
                    cache_bank->transform_block, scan, lambda, dest_block);
            }
#endif

        } break;

        case EVX_BLOCK_INTER_DELTA:
        {
#if EVX_ENABLE_B_FRAMES
            if (frame.type == EVX_FRAME_BIDIR && block_desc->prediction_mode == 2)
            {
                // Bi-predicted: residual against average of fwd and bwd at (i,j)
                macroblock fwd_block, bwd_block;
                create_macroblock(cache_bank->prediction_cache[block_desc->prediction_target], i, j, &fwd_block);
                create_macroblock(cache_bank->prediction_cache[block_desc->prediction_target_b], i, j, &bwd_block);

                for (uint32 jj = 0; jj < EVX_MACROBLOCK_SIZE; ++jj)
                for (uint32 ii = 0; ii < EVX_MACROBLOCK_SIZE; ++ii)
                    cache_bank->staging_block.data_y[jj * cache_bank->staging_block.stride + ii] =
                        (int16)((fwd_block.data_y[jj * fwd_block.stride + ii] + bwd_block.data_y[jj * bwd_block.stride + ii] + 1) >> 1);

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

                sub_transform_macroblock(source_block, cache_bank->staging_block, &cache_bank->transform_block);
            }
            else
#endif
            {
                macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
                uint32 inter_pred_index = block_desc->prediction_target;
#else
                uint32 inter_pred_index = query_prediction_index_by_offset(frame, block_desc->prediction_target);
#endif
                create_macroblock(cache_bank->prediction_cache[inter_pred_index], i, j, &beta_block);
                sub_transform_macroblock(source_block, beta_block, &cache_bank->transform_block);
            }

            block_desc->q_index = query_block_quantization_parameter(frame.quality, cache_bank->transform_block, block_desc->block_type);
            block_desc->variance = compute_block_variance2(cache_bank->transform_block);
            quantize_macroblock(block_desc->q_index, block_desc->block_type, cache_bank->transform_block, dest_block);

#if EVX_ENABLE_TRELLIS_QUANTIZATION
            {
                const uint8 *scan = select_scan_table_8x8(block_desc->intra_mode);
                float lambda = compute_lambda(frame.quality);
                trellis_optimize_macroblock(block_desc->q_index, block_desc->block_type,
                    cache_bank->transform_block, scan, lambda, dest_block);
            }
#endif

        } break;

        case EVX_BLOCK_INTER_MOTION_DELTA:
        {
#if EVX_ENABLE_B_FRAMES
            if (frame.type == EVX_FRAME_BIDIR && block_desc->prediction_mode == 2)
            {
                // Bi-predicted with motion: residual against average of fwd(mv) and bwd(mv_b)
                // Use scratch buffers to avoid corrupting prediction_cache via sub-pixel writes.
                macroblock fwd_block, bwd_block;

                // Forward prediction
                create_macroblock(cache_bank->prediction_cache[block_desc->prediction_target],
                    i + block_desc->motion_x, j + block_desc->motion_y, &fwd_block);
                const macroblock *fwd_ref = &fwd_block;
                if (block_desc->sp_pred)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc->sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[block_desc->prediction_target],
                        i + block_desc->motion_x, j + block_desc->motion_y,
                        sp_i, sp_j, block_desc->sp_amount, &cache_bank->motion_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[block_desc->prediction_target],
                        block_desc->sp_amount, fwd_block,
                        i + block_desc->motion_x + sp_i, j + block_desc->motion_y + sp_j, &cache_bank->motion_block);
#endif
                    fwd_ref = &cache_bank->motion_block;
                }

                // Backward prediction
                create_macroblock(cache_bank->prediction_cache[block_desc->prediction_target_b],
                    i + block_desc->motion_x_b, j + block_desc->motion_y_b, &bwd_block);
                const macroblock *bwd_ref = &bwd_block;
                if (block_desc->sp_pred_b)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc->sp_index_b, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[block_desc->prediction_target_b],
                        i + block_desc->motion_x_b, j + block_desc->motion_y_b,
                        sp_i, sp_j, block_desc->sp_amount_b, &cache_bank->transform_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[block_desc->prediction_target_b],
                        block_desc->sp_amount_b, bwd_block,
                        i + block_desc->motion_x_b + sp_i, j + block_desc->motion_y_b + sp_j, &cache_bank->transform_block);
#endif
                    bwd_ref = &cache_bank->transform_block;
                }

                // Average: (fwd + bwd + 1) >> 1 into staging_block
                for (uint32 jj = 0; jj < EVX_MACROBLOCK_SIZE; ++jj)
                for (uint32 ii = 0; ii < EVX_MACROBLOCK_SIZE; ++ii)
                    cache_bank->staging_block.data_y[jj * cache_bank->staging_block.stride + ii] =
                        (int16)((fwd_ref->data_y[jj * fwd_ref->stride + ii] + bwd_ref->data_y[jj * bwd_ref->stride + ii] + 1) >> 1);

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

                sub_transform_macroblock(source_block, cache_bank->staging_block, &cache_bank->transform_block);
            }
            else
#endif
            {
                macroblock beta_block;
#if EVX_ENABLE_B_FRAMES
                uint32 inter_pred_index = block_desc->prediction_target;
#else
                uint32 inter_pred_index = query_prediction_index_by_offset(frame, block_desc->prediction_target);
#endif
                create_macroblock(cache_bank->prediction_cache[inter_pred_index], i + block_desc->motion_x, j + block_desc->motion_y, &beta_block);

                if (block_desc->sp_pred)
                {
                    int16 sp_i, sp_j;
                    compute_motion_direction_from_frac_index(block_desc->sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
                    interpolate_subpixel_6tap(cache_bank->prediction_cache[inter_pred_index],
                        i + block_desc->motion_x, j + block_desc->motion_y,
                        sp_i, sp_j, block_desc->sp_amount, &cache_bank->motion_block);
#else
                    create_subpixel_macroblock(&cache_bank->prediction_cache[inter_pred_index], block_desc->sp_amount, beta_block,
                                               i + block_desc->motion_x + sp_i, j + block_desc->motion_y + sp_j, &cache_bank->motion_block);
#endif

                    sub_transform_macroblock(source_block, cache_bank->motion_block, &cache_bank->transform_block);
                }
                else
                {
                    // no sub-pixel motion estimation
                    sub_transform_macroblock(source_block, beta_block, &cache_bank->transform_block);
                }
            }

            block_desc->q_index = query_block_quantization_parameter(frame.quality, cache_bank->transform_block, block_desc->block_type);
            block_desc->variance = compute_block_variance2(cache_bank->transform_block);
            quantize_macroblock(block_desc->q_index, block_desc->block_type, cache_bank->transform_block, dest_block);

#if EVX_ENABLE_TRELLIS_QUANTIZATION
            {
                const uint8 *scan = select_scan_table_8x8(block_desc->intra_mode);
                float lambda = compute_lambda(frame.quality);
                trellis_optimize_macroblock(block_desc->q_index, block_desc->block_type,
                    cache_bank->transform_block, scan, lambda, dest_block);
            }
#endif

        } break;

        // The following are primarily handled by the reverse (decode) pipeline.
        case EVX_BLOCK_INTRA_MOTION_COPY:
        case EVX_BLOCK_INTER_MOTION_COPY:
        case EVX_BLOCK_INTER_COPY: break;

        default: return evx_post_error(EVX_ERROR_INVALID_RESOURCE);;
    };

    return EVX_SUCCESS;
}

evx_status encode_slice(const evx_frame &frame, evx_context *context)
{
    uint32 block_index = 0;
    uint32 width = query_context_width(*context);
    uint32 height = query_context_height(*context);
#if EVX_ENABLE_B_FRAMES
    uint32 dest_index = frame.dpb_slot;
#else
    uint32 dest_index = query_prediction_index_by_offset(frame, 0);
#endif

    macroblock source_block, dest_block, dest_prediction_block;

    for (int32 j = 0; j < height; j += EVX_MACROBLOCK_SIZE)
    for (int32 i = 0; i < width;  i += EVX_MACROBLOCK_SIZE)
    {
        evx_block_desc *block_desc = &context->block_table[block_index++];

        create_macroblock(context->cache_bank.input_cache, i, j, &source_block);
        create_macroblock(context->cache_bank.output_cache, i, j, &dest_block);
        create_macroblock(context->cache_bank.prediction_cache[dest_index], i, j, &dest_prediction_block);

        // Classify the block and pass it to the encoding pipeline.
#if EVX_ENABLE_B_FRAMES
        if (frame.type == EVX_FRAME_BIDIR)
        {
            if (evx_failed(classify_block_bidir(frame, source_block, &context->cache_bank, i, j,
                                                 context->block_table, context->width_in_blocks, block_index - 1,
                                                 block_desc)))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }
        }
        else
#endif
        {
            if (evx_failed(classify_block(frame, source_block, &context->cache_bank, i, j,
                                           context->block_table, context->width_in_blocks, block_index - 1,
#if EVX_ENABLE_TEMPORAL_MVP
                                           context->prev_block_table,
#endif
                                           block_desc)))
            {
                return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
            }
        }

        if (evx_failed(encode_block(frame, source_block, &context->cache_bank, i, j, block_desc, &dest_block)))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }

        // The decoder frontend is used as our reverse pipeline. it would be more efficient to
        // update our prediction within encode_block, but we sacrifice for clarity.
        if (evx_failed(decode_block(frame, *block_desc, dest_block, &context->cache_bank, i, j, &dest_prediction_block)))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    return EVX_SUCCESS;
}

#if EVX_ENABLE_WPP

evx_status encode_slice_wpp(const evx_frame &frame, evx_context *context)
{
    uint32 width = query_context_width(*context);
    uint32 height = query_context_height(*context);
    uint32 cols = context->width_in_blocks;
    uint32 rows = context->height_in_blocks;
#if EVX_ENABLE_B_FRAMES
    uint32 dest_index = frame.dpb_slot;
#else
    uint32 dest_index = query_prediction_index_by_offset(frame, 0);
#endif

    uint32 num_threads = std::max(1u, std::min((uint32)std::thread::hardware_concurrency(), rows));
    if (num_threads < 2)
        return encode_slice(frame, context);

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

                // Wait for top-row dependency: row_progress[row-1] >= min(col+2, cols-1)
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

                macroblock source_block, dest_block, dest_prediction_block;
                create_macroblock(thread_bank.input_cache, px, py, &source_block);
                create_macroblock(thread_bank.output_cache, px, py, &dest_block);
                create_macroblock(thread_bank.prediction_cache[dest_index], px, py, &dest_prediction_block);

#if EVX_ENABLE_B_FRAMES
                if (frame.type == EVX_FRAME_BIDIR)
                {
                    if (evx_failed(classify_block_bidir(frame, source_block, &thread_bank, px, py,
                                                         context->block_table, cols, block_index,
                                                         block_desc)))
                    {
                        error_flag.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
                else
#endif
                if (evx_failed(classify_block(frame, source_block, &thread_bank, px, py,
                                               context->block_table, cols, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                                               context->prev_block_table,
#endif
                                               block_desc)))
                {
                    error_flag.store(true, std::memory_order_relaxed);
                    return;
                }

                if (evx_failed(encode_block(frame, source_block, &thread_bank, px, py, block_desc, &dest_block)))
                {
                    error_flag.store(true, std::memory_order_relaxed);
                    return;
                }

                if (evx_failed(decode_block(frame, *block_desc, dest_block, &thread_bank, px, py, &dest_prediction_block)))
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

evx_status engine_encode_frame(const image &input, const evx_frame &frame_desc, evx_context *context, bit_stream *output)
{
    if (evx_failed(convert_image(input, &context->cache_bank.input_cache)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_B_FRAMES
    uint32 dest_index = frame_desc.dpb_slot;
#else
    uint32 dest_index = query_prediction_index_by_offset(frame_desc, 0);
#endif

#if EVX_ENABLE_WPP
    if (evx_failed(encode_slice_wpp(frame_desc, context)))
#else
    if (evx_failed(encode_slice(frame_desc, context)))
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
    if (evx_failed(sao_encode_frame(context->cache_bank.input_cache,
                                     &context->cache_bank.prediction_cache[dest_index],
                                     context->sao_table,
                                     context->width_in_blocks, context->height_in_blocks,
                                     compute_lambda(frame_desc.quality))))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
#endif

    // Serialize our context to the output bitstream.
    if (evx_failed(serialize_slice(frame_desc, context, output)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

#if EVX_ENABLE_TEMPORAL_MVP
#if EVX_ENABLE_B_FRAMES
    if (frame_desc.type != EVX_FRAME_BIDIR)
#endif
    {
        uint32 block_count = context->width_in_blocks * context->height_in_blocks;
        aligned_byte_copy(context->block_table, sizeof(evx_block_desc) * block_count, context->prev_block_table);
    }
#endif

    return EVX_SUCCESS;
}

} // namespace evx
