
#include "common.h"
#include "config.h"
#include "evx_memory.h"
#include "intra_pred.h"
#include "version.h"
#if EVX_ENABLE_SAO
#include "sao.h"
#endif

namespace evx {

evx_status initialize_header(uint32 width, uint32 height, evx_header *header)
{
    // Configure our header with default values.
    header->magic[0] = 'E';
    header->magic[1] = 'V';
    header->magic[2] = 'X';
    header->magic[3] = '1';
    header->ref_count = EVX_REFERENCE_FRAME_COUNT;
    header->version = EVX_VERSION_WORD(EVX_VERSION_MAJOR, EVX_VERSION_MINOR);
    header->frame_width = width;
    header->frame_height = height;
    header->size = sizeof(evx_header);

    return EVX_SUCCESS; 
}

evx_status verify_header(const evx_header &header)
{
    uint16 version = EVX_VERSION_WORD(EVX_VERSION_MAJOR, EVX_VERSION_MINOR);

    if (header.magic[0] != 'E' || header.magic[1] != 'V' ||
        header.magic[2] != 'X' || header.magic[3] != '1')
    {
        return EVX_ERROR_INVALID_RESOURCE;
    }

    if (header.version != version || 
        header.ref_count != EVX_REFERENCE_FRAME_COUNT || 
        header.size != sizeof(header))
    {
        return EVX_ERROR_INVALID_RESOURCE;
    }

    return EVX_SUCCESS;
}

evx_status clear_header(evx_header *header)
{
    return initialize_header(0, 0, header);
}

evx_status clear_frame(evx_frame *frame)
{
    if (EVX_PARAM_CHECK)
    {
        if (!frame)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    frame->type = EVX_FRAME_INTRA;
    frame->index = 0;
    frame->quality = clip_range(EVX_DEFAULT_QUALITY_LEVEL, 1, 100);
#if EVX_ENABLE_B_FRAMES
    frame->display_order = 0;
    frame->is_reference = 0;
    frame->dpb_slot = 0;
    frame->ref_slot_fwd = 0;
    frame->ref_slot_bwd = 0;
#endif

    return EVX_SUCCESS;
}

evx_status clear_block_desc(evx_block_desc *block_desc)
{
    aligned_zero_memory(block_desc, sizeof(evx_block_desc));
    block_desc->block_type = EVX_BLOCK_INTRA_DEFAULT;
    block_desc->intra_mode = 7; // EVX_INTRA_MODE_NONE
#if EVX_ENABLE_B_FRAMES
    block_desc->prediction_mode = 0;
    block_desc->prediction_target_b = 0;
    block_desc->motion_x_b = 0;
    block_desc->motion_y_b = 0;
    block_desc->sp_pred_b = false;
    block_desc->sp_amount_b = false;
    block_desc->sp_index_b = 0;
#endif

    return EVX_SUCCESS;
}

evx_context::evx_context() : block_table(NULL)
#if EVX_ENABLE_TEMPORAL_MVP
    , prev_block_table(NULL)
#endif
#if EVX_ENABLE_SAO
    , sao_table(NULL)
#endif
{
}

evx_status initialize_context(uint32 width, uint32 height, evx_context *context)
{
    if (EVX_PARAM_CHECK)
    {
        if (!context)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    context->width_in_blocks = (width >> EVX_MACROBLOCK_SHIFT);
    context->height_in_blocks = (height >> EVX_MACROBLOCK_SHIFT);
    uint32 block_count = (context->width_in_blocks) * (context->height_in_blocks);

    if (EVX_SUCCESS != context->cache_bank.input_cache.initialize(EVX_IMAGE_FORMAT_R16S, width, height))
    {
        clear_context(context);
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != context->cache_bank.transform_cache.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE))
    {
        clear_context(context);
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != context->cache_bank.motion_cache.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE))
    {
        clear_context(context);
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != context->cache_bank.output_cache.initialize(EVX_IMAGE_FORMAT_R16S, width, height))
    {
        clear_context(context);
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != context->cache_bank.staging_cache.initialize(EVX_IMAGE_FORMAT_R16S, EVX_MACROBLOCK_SIZE, EVX_MACROBLOCK_SIZE))
    {
        clear_context(context);
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    for (uint32 i = 0; i < EVX_REFERENCE_FRAME_COUNT; ++i)
    {
        if (EVX_SUCCESS != context->cache_bank.prediction_cache[i].initialize(EVX_IMAGE_FORMAT_R16S, width, height))
        {
            clear_context(context);
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }
    
    // Create block caches.
    create_macroblock(context->cache_bank.transform_cache, 0, 0, &context->cache_bank.transform_block);
    create_macroblock(context->cache_bank.motion_cache, 0, 0, &context->cache_bank.motion_block);
    create_macroblock(context->cache_bank.staging_cache, 0, 0, &context->cache_bank.staging_block);

    context->block_table = new evx_block_desc[block_count];

    if (!context->block_table)
    {
        clear_context(context);
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    aligned_zero_memory(context->block_table, sizeof(evx_block_desc) * block_count);

#if EVX_ENABLE_TEMPORAL_MVP
    context->prev_block_table = new evx_block_desc[block_count];

    if (!context->prev_block_table)
    {
        clear_context(context);
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    aligned_zero_memory(context->prev_block_table, sizeof(evx_block_desc) * block_count);
#endif

#if EVX_ENABLE_SAO
    context->sao_table = new evx_sao_info[block_count];

    if (!context->sao_table)
    {
        clear_context(context);
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    for (uint32 i = 0; i < block_count; i++)
        clear_sao_info(&context->sao_table[i]);
#endif

    context->feed_stream.resize_capacity(32 * EVX_MB);

    return EVX_SUCCESS;
}

evx_status clear_context(evx_context *context)
{
    if (EVX_PARAM_CHECK)
    {
        if (!context)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    context->cache_bank.input_cache.deinitialize();
    context->cache_bank.output_cache.deinitialize();
    context->cache_bank.transform_cache.deinitialize();
    context->cache_bank.motion_cache.deinitialize();
    context->cache_bank.staging_cache.deinitialize();

    for (uint32 i = 0; i < EVX_REFERENCE_FRAME_COUNT; ++i)
    {
        context->cache_bank.prediction_cache[i].deinitialize();
    }

    delete [] context->block_table;
    context->block_table = NULL;

#if EVX_ENABLE_TEMPORAL_MVP
    delete [] context->prev_block_table;
    context->prev_block_table = NULL;
#endif
#if EVX_ENABLE_SAO
    delete [] context->sao_table;
    context->sao_table = NULL;
#endif
    context->feed_stream.clear();
    context->arith_coder.clear();
    for (int i = 0; i < EVX_NUM_COEFF_CONTEXTS; i++) context->coeff_contexts[i].clear();
#if EVX_ENABLE_UNIFIED_METADATA
    context->meta_contexts.clear();
#endif

    return EVX_SUCCESS;
}

uint32 query_context_width(const evx_context &context)
{
    return context.width_in_blocks << EVX_MACROBLOCK_SHIFT;
}

uint32 query_context_height(const evx_context &context)
{
    return context.height_in_blocks << EVX_MACROBLOCK_SHIFT;
}

uint32 query_prediction_index_by_offset(const evx_frame &frame, uint8 offset)
{
    return (frame.index + EVX_REFERENCE_FRAME_COUNT - offset) % EVX_REFERENCE_FRAME_COUNT;
}

#if EVX_ENABLE_B_FRAMES

uint32 assign_ip_dpb_slot(uint32 ip_index)
{
    return ip_index % 2;
}

uint32 assign_bref_dpb_slot()
{
    return 2;
}

uint32 assign_b_scratch_slot()
{
    return (EVX_B_FRAME_COUNT >= 3) ? 3 : 2;
}

uint32 assign_b_scratch_slot_for_count(uint32 b_frame_count)
{
    return (b_frame_count >= 3) ? 3 : 2;
}

#endif // EVX_ENABLE_B_FRAMES

#if EVX_ENABLE_MPM

void compute_intra_mpm(const evx_block_desc *block_table, uint32 width_in_blocks,
                        uint32 block_index, uint8 mpm[2])
{
    uint32 bx = block_index % width_in_blocks;
    uint32 by = block_index / width_in_blocks;

    uint8 left_mode = EVX_INTRA_MODE_DC;
    uint8 top_mode  = EVX_INTRA_MODE_DC;

    if (bx > 0 && block_table[block_index - 1].block_type == EVX_BLOCK_INTRA_DEFAULT)
        left_mode = block_table[block_index - 1].intra_mode;

    if (by > 0 && block_table[block_index - width_in_blocks].block_type == EVX_BLOCK_INTRA_DEFAULT)
        top_mode = block_table[block_index - width_in_blocks].intra_mode;

    if (left_mode == top_mode)
    {
        mpm[0] = left_mode;
        mpm[1] = (left_mode == EVX_INTRA_MODE_DC) ? EVX_INTRA_MODE_VERT : EVX_INTRA_MODE_DC;
    }
    else
    {
        mpm[0] = left_mode;
        mpm[1] = top_mode;
    }
}

#endif

} // namespace evx