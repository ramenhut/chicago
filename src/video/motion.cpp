
#include "analysis.h"
#include "common.h"
#include "config.h"
#include "macroblock.h"

#if EVX_ENABLE_6TAP_INTERPOLATION
#include "interp.h"
#endif

// Increasing these above zero will allow the encoder to skip blocks that 
// exhibit some degree of variation. Note, this constant has been replaced by
// a quality dependent threshold (see mad_skip_threshold).

// #define EVX_MOTION_MAD_THRESHOLD                 ((EVX_DEFAULT_QUALITY_LEVEL >> 2) + 1)

// Intra prediction searches within the current frame for the closest possible match.
// A motion predicted block carries the overhead of motion vectors, so we only want 
// to use them when they are worth it (EVX_MOTION_MAD_THRESHOLD).
//
// Also, if a motion predicted block is visually identical to our current block, then
// we flag it as a skip block with motion prediction.

#define EVX_MOTION_SAD_THRESHOLD                 (8*EVX_KB)

// Defines our maximum search area around each block. Larger values will require
// significantly more processing but may detect larger movement more effectively.

#define EVX_MOTION_SEARCH_RADIUS                 (16)

namespace evx {

// Sum of Absolute Differences vs Maximum Absolute Difference
//
// We rely upon a SAD operation to determine the closest overall block to our input. 
// This identifies for us the block that will cost the least (overall) to use as our
// prediction. Once we've chosen this block we analyze it using a MAD to determine 
// if it contains any significant enough discrepancies to warrant full use as a non-skip
// block.

#define EVX_PIXEL_DISTANCE_SQ(sx, sy, dx, dy) ((sx-dx)*(sx-dx) + (sy-dy)*(sy-dy))  

typedef struct evx_prediction_params
{
    image_set *prediction;
    int16 mad_skip_threshold;
    int16 pixel_x;
    int16 pixel_y;
    float lambda;

} evx_prediction_params;

typedef struct evx_motion_selection
{
    int16 best_x;
    int16 best_y;
    int32 best_sad;
    int32 best_mad;
    int32 best_ssd;
    float best_cost;
    int16 sp_index;

    bool sp_amount;
    bool sp_enabled;

} evx_motion_selection;

static inline int16 compute_motion_frac_index_from_direction(int16 i, int16 j)
{
    i++, j++;
    
    switch (j)
    {
        case 0: return i;
        case 1:
        {
            switch (i)
            {
                case 0: return 3;
                case 2: return 4;
            };

        } break;

        case 2: return i+5;
    };

    evx_post_error(EVX_ERROR_INVALIDARG);

    return 0;
}

void compute_motion_direction_from_frac_index(int16 frac_index, int16 *dir_x, int16 *dir_y)
{
    switch (frac_index)
    {
        case 0:
        case 1:
        case 2:
            *dir_y = -1;
            *dir_x = frac_index - 1;
            return;

        case 3: *dir_x = -1; *dir_y = 0; return;
        case 4: *dir_x = 1; *dir_y = 0; return;

        case 5:
        case 6:
        case 7:
            *dir_y = 1;
            *dir_x = frac_index - 6;
            return;
    };

    evx_post_error(EVX_ERROR_INVALIDARG);
}

// Estimate the bit cost of encoding a motion vector (mv_x, mv_y).
// Uses L1 magnitude as proxy for entropy-coded bit cost.
static inline float estimate_mv_bits(int16 mv_x, int16 mv_y)
{
    return (float)(abs(mv_x) + abs(mv_y) + 1);
}

static inline void evaluate_motion_candidate(int32 current_x, int32 current_y, const evx_prediction_params &params,
                                             const macroblock &src_block, evx_motion_selection *selection)
{
    macroblock test_block;

    create_macroblock(*params.prediction, current_x, current_y, &test_block);
    int32 current_sad = compute_block_sad(src_block, test_block);
    int32 current_ssd = EVX_PIXEL_DISTANCE_SQ(current_x, current_y, params.pixel_x, params.pixel_y);
    int32 current_mad = compute_block_mad(src_block, test_block);

    // Compute RD cost when lambda > 0: SATD + lambda * MV_bits.
    float current_cost = (float)current_sad;

#if EVX_ENABLE_SATD_METRIC
    if (params.lambda > 0.0f)
    {
        int16 mv_x = (int16)(current_x - params.pixel_x);
        int16 mv_y = (int16)(current_y - params.pixel_y);
        int32 satd = compute_block_satd(src_block, test_block);
        current_cost = (float)satd + params.lambda * estimate_mv_bits(mv_x, mv_y);
    }
#endif

    // If we've already found a suitable copy block, then we only accept new
    // candidates that are a closer matched copy block.
    if (selection->best_mad < params.mad_skip_threshold)
    {
        if (current_mad < selection->best_mad ||
            (current_mad == selection->best_mad &&
             current_ssd < selection->best_ssd))
        {
            selection->best_x = current_x;
            selection->best_y = current_y;
            selection->best_sad = current_sad;
            selection->best_ssd = current_ssd;
            selection->best_mad = current_mad;
            selection->best_cost = current_cost;
        }
    }
    else
    {
        if ((current_cost < selection->best_cost ||
            (current_cost == selection->best_cost && current_ssd < selection->best_ssd) &&
            current_sad < EVX_MOTION_SAD_THRESHOLD) || current_mad < params.mad_skip_threshold)
        {
            selection->best_x = current_x;
            selection->best_y = current_y;
            selection->best_sad = current_sad;
            selection->best_ssd = current_ssd;
            selection->best_mad = compute_block_mad(src_block, test_block);
            selection->best_cost = current_cost;
        }
    }
}

static inline void evaluate_subpel_motion_candidate(int32 target_x, int32 target_y, int16 i, int16 j, const evx_prediction_params &params,
                                                    const macroblock &src_block, macroblock *cache_block, const macroblock &best_block,
                                                    evx_motion_selection *selection)
{
    // Half-pel evaluation.
#if EVX_ENABLE_6TAP_INTERPOLATION
    interpolate_subpixel_6tap(*params.prediction, selection->best_x, selection->best_y,
                               i, j, false, cache_block);
#else
    macroblock test_block;
    create_macroblock(*params.prediction, target_x, target_y, &test_block);
    lerp_macroblock_half(best_block, test_block, cache_block);
#endif
    int32 current_sad = compute_block_sad(src_block, *cache_block);
    int32 current_mad = compute_block_mad(src_block, *cache_block);

    // If we've already found a suitable copy block, then we only accept new
    // candidates that are a closer matched copy block.
    if (selection->best_mad < params.mad_skip_threshold)
    {
        if (current_mad < selection->best_mad)
        {
            selection->sp_enabled = true;
            selection->sp_amount = false;   // identifies a half pixel interpolation.
            selection->sp_index = compute_motion_frac_index_from_direction(i, j);

            selection->best_sad = current_sad;
            selection->best_mad = current_mad;
        }
    }
    else
    {
        if ((current_sad < selection->best_sad && current_sad < EVX_MOTION_SAD_THRESHOLD) ||
            current_mad < params.mad_skip_threshold)
        {
            selection->sp_enabled = true;
            selection->sp_amount = false;   // identifies a half pixel interpolation.
            selection->sp_index = compute_motion_frac_index_from_direction(i, j);

            selection->best_sad = current_sad;
            selection->best_mad = current_mad;
        }
    }

    // Quarter-pel evaluation.
#if EVX_ENABLE_6TAP_INTERPOLATION
    interpolate_subpixel_6tap(*params.prediction, selection->best_x, selection->best_y,
                               i, j, true, cache_block);
#else
    lerp_macroblock_quarter(best_block, test_block, cache_block);
#endif
    current_sad = compute_block_sad(src_block, *cache_block);
    current_mad = compute_block_mad(src_block, *cache_block);

    // If we've already found a suitable copy block, then we only accept new
    // candidates that are a closer matched copy block.
    if (selection->best_mad < params.mad_skip_threshold)
    {
        if (current_mad < selection->best_mad)
        {
            selection->sp_enabled = true;
            selection->sp_amount = true;   // identifies a quarter pixel interpolation.
            selection->sp_index = compute_motion_frac_index_from_direction(i, j);

            selection->best_sad = current_sad;
            selection->best_mad = current_mad;
        }
    }
    else
    {
        if ((current_sad < selection->best_sad && current_sad < EVX_MOTION_SAD_THRESHOLD) ||
            current_mad < params.mad_skip_threshold)
        {
            selection->sp_enabled = true;
            selection->sp_amount = true;   // identifies a quarter pixel interpolation.
            selection->sp_index = compute_motion_frac_index_from_direction(i, j);

            selection->best_sad = current_sad;
            selection->best_mad = current_mad;
        }
    }
}

static inline int16 median3(int16 a, int16 b, int16 c)
{
    if (a > b) { int16 t = a; a = b; b = t; }
    if (b > c) { b = c; }
    if (a > b) { b = a; }
    return b;
}

void compute_mv_predictor(const evx_block_desc *block_table, uint32 width_in_blocks,
                           uint32 block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                           const evx_block_desc *prev_block_table,
#endif
                           int16 *pred_x, int16 *pred_y)
{
    uint32 bx = block_index % width_in_blocks;
    uint32 by = block_index / width_in_blocks;

    int16 mvx[3], mvy[3];
    int count = 0;

    // Left neighbor
    if (bx > 0 && EVX_IS_MOTION_BLOCK_TYPE(block_table[block_index - 1].block_type))
    {
        mvx[count] = block_table[block_index - 1].motion_x;
        mvy[count] = block_table[block_index - 1].motion_y;
        count++;
    }

    // Top neighbor
    if (by > 0 && EVX_IS_MOTION_BLOCK_TYPE(block_table[block_index - width_in_blocks].block_type))
    {
        mvx[count] = block_table[block_index - width_in_blocks].motion_x;
        mvy[count] = block_table[block_index - width_in_blocks].motion_y;
        count++;
    }

    // Top-right neighbor (fallback to top-left if unavailable)
    if (by > 0 && (bx + 1) < width_in_blocks &&
        EVX_IS_MOTION_BLOCK_TYPE(block_table[block_index - width_in_blocks + 1].block_type))
    {
        mvx[count] = block_table[block_index - width_in_blocks + 1].motion_x;
        mvy[count] = block_table[block_index - width_in_blocks + 1].motion_y;
        count++;
    }
    else if (by > 0 && bx > 0 &&
             EVX_IS_MOTION_BLOCK_TYPE(block_table[block_index - width_in_blocks - 1].block_type))
    {
        mvx[count] = block_table[block_index - width_in_blocks - 1].motion_x;
        mvy[count] = block_table[block_index - width_in_blocks - 1].motion_y;
        count++;
    }

#if EVX_ENABLE_TEMPORAL_MVP
    if (count < 3 && prev_block_table &&
        EVX_IS_MOTION_BLOCK_TYPE(prev_block_table[block_index].block_type))
    {
        mvx[count] = prev_block_table[block_index].motion_x;
        mvy[count] = prev_block_table[block_index].motion_y;
        count++;
    }
#endif

    if (count == 0)
    {
        *pred_x = 0;
        *pred_y = 0;
    }
    else if (count == 1)
    {
        *pred_x = mvx[0];
        *pred_y = mvy[0];
    }
    else if (count == 2)
    {
        *pred_x = (mvx[0] + mvx[1]) / 2;
        *pred_y = (mvy[0] + mvy[1]) / 2;
    }
    else
    {
        *pred_x = median3(mvx[0], mvx[1], mvx[2]);
        *pred_y = median3(mvy[0], mvy[1], mvy[2]);
    }
}

void perform_intra_motion_search(int16 left, int16 top, int16 right, int16 bottom, int16 step,
                                 const evx_prediction_params &params, const macroblock &src_block,
                                 evx_motion_selection *selection)
{
    int32 base_x = selection->best_x;
    int32 base_y = selection->best_y;

    for (int16 j = top; j <= bottom; j += step)
    for (int16 i = left; i <= right; i += step)
    {
        int32 current_x = base_x + i;
        int32 current_y = base_y + j;

        if (current_y > (params.pixel_y - EVX_MACROBLOCK_SIZE) &&
            current_x > (params.pixel_x - EVX_MACROBLOCK_SIZE))
        {
             continue;
        }

        if (current_x < 0 || current_x > params.prediction->query_width() - EVX_MACROBLOCK_SIZE ||
            current_y < 0 || current_y > params.prediction->query_height() - EVX_MACROBLOCK_SIZE)
        {
             continue;
        }

        evaluate_motion_candidate(current_x, current_y, params, src_block, selection);

        // Early exit if we found a copy-quality match.
        if (selection->best_mad < params.mad_skip_threshold)
            return;
    }
}

void perform_inter_motion_search(int16 left, int16 top, int16 right, int16 bottom, int16 step,
                                 const evx_prediction_params &params, const macroblock &src_block,
                                 evx_motion_selection *selection)
{
    int32 base_x = selection->best_x;
    int32 base_y = selection->best_y;

    for (int16 j = top; j <= bottom; j += step)
    for (int16 i = left; i <= right; i += step)
    {
        int32 current_x = base_x + i;
        int32 current_y = base_y + j;

        if (current_x < 0 || current_x > params.prediction->query_width() - EVX_MACROBLOCK_SIZE ||
            current_y < 0 || current_y > params.prediction->query_height() - EVX_MACROBLOCK_SIZE)
        {
             continue;
        }

        evaluate_motion_candidate(current_x, current_y, params, src_block, selection);

        // Early exit if we found a copy-quality match.
        if (selection->best_mad < params.mad_skip_threshold)
            return;
    }
}

void perform_intra_subpixel_motion_search(const evx_prediction_params &params, 
                                          const macroblock &src_block, 
                                          macroblock *cache_block,
                                          evx_motion_selection *selection)
{
    macroblock best_block;  // determined by pixel level motion analysis.
    create_macroblock(*params.prediction, selection->best_x, selection->best_y, &best_block);

    // ensure that the selection will contain no subpixel prediction if a better match is not found.
    selection->sp_index = 0;
    selection->sp_amount = false;
    selection->sp_enabled = false;

    // perform a search of neighboring subpixel blocks using biliear interpolation.
    for (int16 j = -1; j <= 1; ++j)                                                                     
    for (int16 i = -1; i <= 1; ++i)                                                                     
    {                                                                                                   
        int32 target_x = selection->best_x + i;                                                                    
        int32 target_y = selection->best_y + j;                                                                    
                                                                                                        
        if (0 == i && 0 == j)                                                                           
        {                                                                                               
            continue;                                                                                   
        }                                                                                               
        
        // if we've stepped beyond the stable portion of our intra frame, continue.
        if (target_y > (params.pixel_y - EVX_MACROBLOCK_SIZE) && 
            target_x > (params.pixel_x - EVX_MACROBLOCK_SIZE))   
        {                                                                                               
             continue;                                                                                  
        }                                                                                               
                                                                                                        
        if (target_x < 0 || target_x > params.prediction->query_width() - EVX_MACROBLOCK_SIZE ||                
            target_y < 0 || target_y > params.prediction->query_height() - EVX_MACROBLOCK_SIZE)                 
        {                                                                                               
            continue;                                                                                   
        }                                                                                               

        evaluate_subpel_motion_candidate(target_x, target_y, i, j, params, src_block, cache_block, best_block, selection);
    } 
}

void perform_inter_subpixel_motion_search(const evx_prediction_params &params, 
                                          const macroblock &src_block, 
                                          macroblock *cache_block,
                                          evx_motion_selection *selection)
{
    macroblock best_block;  // determined by pixel level motion analysis.
    create_macroblock(*params.prediction, selection->best_x, selection->best_y, &best_block);

    // ensure that the selection will contain no subpixel prediction if a better match is not found.
    selection->sp_index = 0;
    selection->sp_amount = false;
    selection->sp_enabled = false;

    // perform a search of neighboring subpixel blocks using biliear interpolation.
    for (int16 j = -1; j <= 1; ++j)                                                                     
    for (int16 i = -1; i <= 1; ++i)                                                                     
    {                                                                                                   
        int32 target_x = selection->best_x + i;                                                                    
        int32 target_y = selection->best_y + j;                                                                    
                                                                                                        
        if (0 == i && 0 == j)                                                                           
        {                                                                                               
            continue;                                                                                   
        }                                                                                                                                                                                             
                                                                                                        
        if (target_x < 0 || target_x > params.prediction->query_width() - EVX_MACROBLOCK_SIZE ||                
            target_y < 0 || target_y > params.prediction->query_height() - EVX_MACROBLOCK_SIZE)                 
        {                                                                                               
            continue;                                                                                   
        }                                                                                               
                                                                                
        evaluate_subpel_motion_candidate(target_x, target_y, i, j, params, src_block, cache_block, best_block, selection);
    } 
}

int32 calculate_intra_prediction(const evx_frame &frame, const macroblock &src_block, int32 pixel_x, int32 pixel_y, evx_cache_bank *cache_bank, evx_block_desc *output_desc)
{
    evx_motion_selection selection;
    selection.best_x = pixel_x;
    selection.best_y = pixel_y;
    selection.best_sad = compute_block_sad(src_block);
    selection.best_mad = EVX_MAX_INT32;
    selection.best_ssd = EVX_MAX_INT32;
    selection.best_cost = (float)selection.best_sad;
    selection.sp_amount = 0;
    selection.sp_index = 0;
    selection.sp_enabled = false;

    evx_prediction_params params;
    params.pixel_x = pixel_x;
    params.pixel_y = pixel_y;
    params.mad_skip_threshold = ((frame.quality >> 2) + 1);

#if EVX_ENABLE_SATD_METRIC
    params.lambda = compute_lambda(frame.quality);
#else
    params.lambda = 0.0f;
#endif

    // Search for the closest match to our current source block within the prediction image.
#if EVX_ENABLE_B_FRAMES
    uint32 intra_pred_index = frame.dpb_slot;
#else
    uint32 intra_pred_index = query_prediction_index_by_offset(frame, 0);
#endif
    params.prediction = const_cast<image_set *>(&cache_bank->prediction_cache[intra_pred_index]);

    // Scan the following values in a triangle around our pixel:
    //                                                           
    //      X          X          X                                                  
    //                                                        
    //      X          X          X                                                   
    //                                                        
    //      X        Pixel  

    // initial scan around our current pixel
    perform_intra_motion_search(-EVX_MOTION_SEARCH_RADIUS, -(EVX_MOTION_SEARCH_RADIUS << 1),
                                EVX_MOTION_SEARCH_RADIUS, 0, EVX_MOTION_SEARCH_RADIUS,
                                params, src_block, &selection);

    if (selection.best_mad >= params.mad_skip_threshold)
    {
        for (int16 i = (EVX_MOTION_SEARCH_RADIUS >> 1); i > 0; i >>= 1)
        {
            perform_intra_motion_search(-i, -i, i, i, i, params, src_block, &selection);
            if (selection.best_mad < params.mad_skip_threshold) break;
        }
    }

    // perform sub-pixel motion estimation
    perform_intra_subpixel_motion_search(params, src_block, &cache_bank->motion_block, &selection);    

    // Fill out our block descriptor using our closest match.
    clear_block_desc(output_desc);

    EVX_SET_INTRA_BLOCK_TYPE_BIT(output_desc->block_type, true);

    if (selection.best_x != pixel_x || selection.best_y != pixel_y || selection.sp_enabled)
    {
        EVX_SET_MOTION_BLOCK_TYPE_BIT(output_desc->block_type, true);
    }

    if (selection.best_mad < params.mad_skip_threshold)
    {
        EVX_SET_COPY_BLOCK_TYPE_BIT(output_desc->block_type, true);
    }

    output_desc->prediction_target = 0;
    output_desc->motion_x = selection.best_x - pixel_x;
    output_desc->motion_y = selection.best_y - pixel_y;
    output_desc->sp_pred = selection.sp_enabled;
    output_desc->sp_amount = selection.sp_amount;
    output_desc->sp_index = selection.sp_index;

    return selection.best_sad;
}

int32 perform_inter_search(const evx_frame &frame, const macroblock &src_block, int32 pixel_x, int32 pixel_y,
                           evx_cache_bank *cache_bank, uint32 cache_index,
                           const evx_block_desc *block_table, uint32 width_in_blocks, uint32 block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                           const evx_block_desc *prev_block_table,
#endif
                           evx_block_desc *output_desc)
{
    evx_motion_selection selection;
    selection.best_x = pixel_x;
    selection.best_y = pixel_y;
    selection.best_sad = EVX_MAX_INT32;
    selection.best_mad = EVX_MAX_INT32;
    selection.best_ssd = EVX_MAX_INT32;
    selection.best_cost = (float)EVX_MAX_INT32;
    selection.sp_amount = 0;
    selection.sp_index = 0;
    selection.sp_enabled = false;

    evx_prediction_params params;
    params.pixel_x = pixel_x;
    params.pixel_y = pixel_y;
    params.mad_skip_threshold = ((frame.quality >> 2) + 1);

#if EVX_ENABLE_SATD_METRIC
    params.lambda = compute_lambda(frame.quality);
#else
    params.lambda = 0.0f;
#endif

    // Use the provided cache index directly (no offset conversion)
    params.prediction = const_cast<image_set *>(&cache_bank->prediction_cache[cache_index]);

    macroblock test_block;
    create_macroblock(*params.prediction, pixel_x, pixel_y, &test_block);
    selection.best_sad = compute_block_sad(src_block, test_block);
    selection.best_mad = compute_block_mad(src_block, test_block);
    selection.best_cost = (float)selection.best_sad;

#if EVX_ENABLE_SATD_METRIC
    if (params.lambda > 0.0f)
    {
        selection.best_cost = (float)compute_block_satd(src_block, test_block)
                            + params.lambda * estimate_mv_bits(0, 0);
    }

    {
        int16 pred_mx = 0, pred_my = 0;
        compute_mv_predictor(block_table, width_in_blocks, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                              prev_block_table,
#endif
                              &pred_mx, &pred_my);

        if (pred_mx != 0 || pred_my != 0)
        {
            int32 pred_x = pixel_x + pred_mx;
            int32 pred_y = pixel_y + pred_my;

            int32 max_x = params.prediction->query_width() - EVX_MACROBLOCK_SIZE;
            int32 max_y = params.prediction->query_height() - EVX_MACROBLOCK_SIZE;
            if (pred_x < 0) pred_x = 0;
            if (pred_x > max_x) pred_x = max_x;
            if (pred_y < 0) pred_y = 0;
            if (pred_y > max_y) pred_y = max_y;

            evaluate_motion_candidate(pred_x, pred_y, params, src_block, &selection);
        }
    }
#endif

    if (selection.best_mad >= params.mad_skip_threshold)
    {
        int16 search_radius = EVX_MOTION_SEARCH_RADIUS;
#if EVX_ENABLE_ADAPTIVE_SEARCH_RADIUS
        if (selection.best_sad < EVX_MOTION_SAD_THRESHOLD / 4)
            search_radius = EVX_MOTION_SEARCH_RADIUS / 4;
        else if (selection.best_sad < EVX_MOTION_SAD_THRESHOLD / 2)
            search_radius = EVX_MOTION_SEARCH_RADIUS / 2;
#endif

        for (int16 i = search_radius; i > 0; i >>= 1)
        {
            perform_inter_motion_search(-i, -i, i, i, i, params, src_block, &selection);
            if (selection.best_mad < params.mad_skip_threshold) break;
        }

        perform_inter_subpixel_motion_search(params, src_block, &cache_bank->motion_block, &selection);
    }

    clear_block_desc(output_desc);

    EVX_SET_INTRA_BLOCK_TYPE_BIT(output_desc->block_type, false);

    if (selection.best_x != pixel_x || selection.best_y != pixel_y || selection.sp_enabled)
    {
        EVX_SET_MOTION_BLOCK_TYPE_BIT(output_desc->block_type, true);
    }

    if (selection.best_mad < params.mad_skip_threshold)
    {
        EVX_SET_COPY_BLOCK_TYPE_BIT(output_desc->block_type, true);
    }

    // NOTE: prediction_target is NOT set here — the caller must set it
    output_desc->motion_x = selection.best_x - pixel_x;
    output_desc->motion_y = selection.best_y - pixel_y;
    output_desc->sp_pred = selection.sp_enabled;
    output_desc->sp_amount = selection.sp_amount;
    output_desc->sp_index = selection.sp_index;

    return selection.best_sad;
}

int32 calculate_inter_prediction(const evx_frame &frame, const macroblock &src_block, int32 pixel_x, int32 pixel_y,
                                 evx_cache_bank *cache_bank, uint16 pred_offset,
                                 const evx_block_desc *block_table, uint32 width_in_blocks, uint32 block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                                 const evx_block_desc *prev_block_table,
#endif
                                 evx_block_desc *output_desc)
{
    uint32 cache_index = query_prediction_index_by_offset(frame, pred_offset);
    int32 sad = perform_inter_search(frame, src_block, pixel_x, pixel_y, cache_bank,
        cache_index, block_table, width_in_blocks, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
        prev_block_table,
#endif
        output_desc);
    output_desc->prediction_target = pred_offset;
    return sad;
}

#if EVX_ENABLE_B_FRAMES

int32 calculate_bidir_prediction(const evx_frame &frame, const macroblock &src_block,
                                  int32 pixel_x, int32 pixel_y,
                                  evx_cache_bank *cache_bank,
                                  uint8 fwd_slot, uint8 bwd_slot,
                                  const evx_block_desc *block_table,
                                  uint32 width_in_blocks, uint32 block_index,
                                  evx_block_desc *output_desc)
{
    // Forward search
    evx_block_desc fwd_desc;
    int32 fwd_sad = perform_inter_search(frame, src_block, pixel_x, pixel_y, cache_bank,
        fwd_slot, block_table, width_in_blocks, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
        nullptr,
#endif
        &fwd_desc);

    // Backward search
    evx_block_desc bwd_desc;
    int32 bwd_sad = perform_inter_search(frame, src_block, pixel_x, pixel_y, cache_bank,
        bwd_slot, block_table, width_in_blocks, block_index,
#if EVX_ENABLE_TEMPORAL_MVP
        nullptr,
#endif
        &bwd_desc);

    // Build forward prediction macroblock (use scratch to avoid corrupting prediction_cache)
    macroblock fwd_block;
    create_macroblock(cache_bank->prediction_cache[fwd_slot],
        pixel_x + fwd_desc.motion_x, pixel_y + fwd_desc.motion_y, &fwd_block);
    const macroblock *fwd_ref = &fwd_block;
    if (fwd_desc.sp_pred)
    {
        int16 sp_i, sp_j;
        compute_motion_direction_from_frac_index(fwd_desc.sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
        interpolate_subpixel_6tap(cache_bank->prediction_cache[fwd_slot],
            pixel_x + fwd_desc.motion_x, pixel_y + fwd_desc.motion_y,
            sp_i, sp_j, fwd_desc.sp_amount, &cache_bank->motion_block);
#else
        macroblock temp_block;
        create_macroblock(cache_bank->prediction_cache[fwd_slot],
            pixel_x + fwd_desc.motion_x + sp_i, pixel_y + fwd_desc.motion_y + sp_j, &temp_block);
        if (fwd_desc.sp_amount)
            lerp_macroblock_quarter(fwd_block, temp_block, &cache_bank->motion_block);
        else
            lerp_macroblock_half(fwd_block, temp_block, &cache_bank->motion_block);
#endif
        fwd_ref = &cache_bank->motion_block;
    }

    // Build backward prediction macroblock (use scratch to avoid corrupting prediction_cache)
    macroblock bwd_block;
    create_macroblock(cache_bank->prediction_cache[bwd_slot],
        pixel_x + bwd_desc.motion_x, pixel_y + bwd_desc.motion_y, &bwd_block);
    const macroblock *bwd_ref = &bwd_block;
    if (bwd_desc.sp_pred)
    {
        int16 sp_i, sp_j;
        compute_motion_direction_from_frac_index(bwd_desc.sp_index, &sp_i, &sp_j);
#if EVX_ENABLE_6TAP_INTERPOLATION
        interpolate_subpixel_6tap(cache_bank->prediction_cache[bwd_slot],
            pixel_x + bwd_desc.motion_x, pixel_y + bwd_desc.motion_y,
            sp_i, sp_j, bwd_desc.sp_amount, &cache_bank->transform_block);
#else
        macroblock temp_block;
        create_macroblock(cache_bank->prediction_cache[bwd_slot],
            pixel_x + bwd_desc.motion_x + sp_i, pixel_y + bwd_desc.motion_y + sp_j, &temp_block);
        if (bwd_desc.sp_amount)
            lerp_macroblock_quarter(bwd_block, temp_block, &cache_bank->transform_block);
        else
            lerp_macroblock_half(bwd_block, temp_block, &cache_bank->transform_block);
#endif
        bwd_ref = &cache_bank->transform_block;
    }

    // Average fwd and bwd: (fwd + bwd + 1) >> 1, compute SAD against source
    int32 bi_sad = 0;

    // Luma: 16x16
    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)
    {
        int32 fv = fwd_ref->data_y[j * fwd_ref->stride + i];
        int32 bv = bwd_ref->data_y[j * bwd_ref->stride + i];
        int32 av = (fv + bv + 1) >> 1;
        cache_bank->staging_block.data_y[j * cache_bank->staging_block.stride + i] = (int16)av;
        bi_sad += abs(src_block.data_y[j * src_block.stride + i] - av);
    }

    // Chroma: 8x8
    uint32 chroma_size = EVX_MACROBLOCK_SIZE >> 1;
    for (uint32 j = 0; j < chroma_size; ++j)
    for (uint32 i = 0; i < chroma_size; ++i)
    {
        uint32 cs = cache_bank->staging_block.stride >> 1;
        uint32 fs = fwd_ref->stride >> 1;
        uint32 bs = bwd_ref->stride >> 1;
        uint32 ss = src_block.stride >> 1;

        int32 fu = fwd_ref->data_u[j * fs + i];
        int32 bu = bwd_ref->data_u[j * bs + i];
        int32 au = (fu + bu + 1) >> 1;
        cache_bank->staging_block.data_u[j * cs + i] = (int16)au;
        bi_sad += abs(src_block.data_u[j * ss + i] - au);

        int32 fv2 = fwd_ref->data_v[j * fs + i];
        int32 bv2 = bwd_ref->data_v[j * bs + i];
        int32 av2 = (fv2 + bv2 + 1) >> 1;
        cache_bank->staging_block.data_v[j * cs + i] = (int16)av2;
        bi_sad += abs(src_block.data_v[j * ss + i] - av2);
    }

    // Fill output descriptor with bi-predicted mode
    clear_block_desc(output_desc);
    output_desc->block_type = EVX_BLOCK_INTER_MOTION_DELTA;
    output_desc->prediction_mode = 2;  // bi-predicted

    // Forward MV (primary)
    output_desc->prediction_target = fwd_slot;
    output_desc->motion_x = fwd_desc.motion_x;
    output_desc->motion_y = fwd_desc.motion_y;
    output_desc->sp_pred = fwd_desc.sp_pred;
    output_desc->sp_amount = fwd_desc.sp_amount;
    output_desc->sp_index = fwd_desc.sp_index;

    // Backward MV (secondary)
    output_desc->prediction_target_b = bwd_slot;
    output_desc->motion_x_b = bwd_desc.motion_x;
    output_desc->motion_y_b = bwd_desc.motion_y;
    output_desc->sp_pred_b = bwd_desc.sp_pred;
    output_desc->sp_amount_b = bwd_desc.sp_amount;
    output_desc->sp_index_b = bwd_desc.sp_index;

    (void)fwd_sad;
    (void)bwd_sad;

    return bi_sad;
}

#endif // EVX_ENABLE_B_FRAMES

} // namespace evx