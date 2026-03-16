
#include "base.h"
#include "evx3.h"
#include "analysis.h"
#include "common.h"
#include "config.h"
#include "quantize.h"
#include "imageset.h"

#define EVX_DEBLOCK_STEP_SIZE                   (8)

namespace evx {

static const int16 alpha_table[EVX_MAX_MPEG_QUANT_LEVELS] =
{
     0,  0,  0,  0,  0,  0,  0,  1,
     1,  1,  2,  2,  3,  3,  4,  5,
     6,  7,  8,  9, 10, 12, 14, 16,
    18, 20, 22, 24, 26, 29, 32, 35,
    38, 42, 46, 50, 54, 59, 64, 70,
    76, 82, 89, 96,104,112,120,128,
   138,148,158,170,182,196,210,224,
   240,255,255,255,255,255,255,255,
};

static const int16 beta_table[EVX_MAX_MPEG_QUANT_LEVELS] =
{
     0,  0,  0,  0,  0,  0,  0,  0,
     1,  1,  1,  1,  2,  2,  2,  3,
     3,  3,  4,  4,  4,  5,  5,  6,
     6,  7,  7,  8,  8,  9, 10, 11,
    12, 13, 14, 15, 16, 17, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18,
};

bool has_identical_motion_state(const evx_block_desc &left, const evx_block_desc &right)
{
    bool motion_left = EVX_IS_MOTION_BLOCK_TYPE(left.block_type);
    bool motion_right = EVX_IS_MOTION_BLOCK_TYPE(right.block_type);

    if (motion_left == motion_right && 
        left.motion_x == right.motion_x && 
        left.motion_y == right.motion_y)
    {
        return true;
    }
    
    return false;
}

static inline uint32 deblock_macroblock_index(uint32 i, uint32 j, uint32 macroblock_size, uint32 width_in_blocks)
{
    return (i / macroblock_size) + (j / macroblock_size) * width_in_blocks;
}

uint8 compute_average_qp(const evx_block_desc &left, const evx_block_desc &right)
{
    if (!EVX_IS_COPY_BLOCK_TYPE(left.block_type) && !EVX_IS_COPY_BLOCK_TYPE(right.block_type))
    {
        return (left.q_index + right.q_index) >> 1;
    }
    else if (!EVX_IS_COPY_BLOCK_TYPE(left.block_type))
    {
        return left.q_index;
    }
    else if (!EVX_IS_COPY_BLOCK_TYPE(right.block_type))
    {
        return right.q_index;
    }

    return 0;
}

uint8 compute_deblock_strength(bool is_mb_boundary, const evx_block_desc &left, const evx_block_desc &right)
{
    bool is_left_copy = EVX_IS_COPY_BLOCK_TYPE(left.block_type);
    bool is_right_copy = EVX_IS_COPY_BLOCK_TYPE(right.block_type);

    if (is_left_copy && is_right_copy)
         return 0;

    if (is_left_copy ^ is_right_copy)
        return 1;

    // At internal sub-block boundaries (same macroblock), cap at strength 1.
    return is_mb_boundary ? 2 : 1;
}

void deblock_filter_values(
    int16 p3, int16 p2, int16 p1, int16 p0, 
    int16 q0, int16 q1, int16 q2, int16 q3,
    int16 *out_p2, int16 *out_p1, int16 *out_p0,
    int16 *out_q0, int16 *out_q1, int16 *out_q2,
    uint8 average_qp, uint8 strength, bool is_luma)
{
    // For strong boundaries at high QP, boost the alpha/beta index to allow more aggressive filtering.
    uint8 qp_index = average_qp;
    if (strength == 2 && average_qp > 0)
    {
        qp_index = (uint8)evx_min2((int16)average_qp + EVX_DEBLOCK_QP_OFFSET, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));
    }

    int16 delta_p0q0 = abs(p0 - q0);
    int16 delta_p1p0 = abs(p1 - p0);
    int16 delta_q1q0 = abs(q1 - q0);
    int16 delta_p2p0 = abs(p2 - p0);
    int16 delta_q2q0 = abs(q2 - q0);

    if (delta_p0q0 >= alpha_table[qp_index] ||
        delta_p1p0 >= beta_table[qp_index] ||
        delta_q1q0 >= beta_table[qp_index])
        return;

    switch (strength)
    {
        case 2:
        {
            (*out_p0) = rounded_div(p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1, 8);
            (*out_p1) = rounded_div(p2 + p1 + p0 + q0, 4);
            (*out_q0) = rounded_div( p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2, 8);
            (*out_q1) = rounded_div( p0 + q0 + q1 + q2, 4);
            
            if (is_luma)
            {
                (*out_p2) = rounded_div(2 * p3 + 3 * p2 + p1 + p0 + q0, 8);
                (*out_q2) = rounded_div( 2 * q3 + 3 * q2 + q1 + q0 + p0, 8);
            }

        } break;

        case 1:
        {
            (*out_p0) = rounded_div(((q0 + p0) * 4) + p1 - q1, 8);
            (*out_q0) = rounded_div(((q0 + p0) * 4) + q1 - p1, 8); 
            
            if (is_luma)
            {
                (*out_p1) = rounded_div((p2 * 4) + (p0 * 2) + (q0 * 2), 8);
                (*out_q1) = rounded_div((q2 * 4) + (q0 * 2) + (p0 * 2), 8);
            }

        } break;
    };
}

void deblock_vertical_edge(int16 *src_data, int32 src_width, uint8 average_qp, uint8 strength, bool is_luma, int16 qp_offset = 0)
{
    uint8 effective_qp = (uint8)evx_min2((int16)average_qp + qp_offset, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));

    for (uint8 i = 0; i < EVX_DEBLOCK_STEP_SIZE; i++)
    {
        int16 q0 = src_data[0];
        int16 q1 = src_data[1];
        int16 q2 = src_data[2];
        int16 q3 = src_data[3];

        int16 p0 = src_data[-1];
        int16 p1 = src_data[-2];
        int16 p2 = src_data[-3];
        int16 p3 = src_data[-4];

        deblock_filter_values(p3, p2, p1, p0, q0, q1, q2, q3,
            src_data - 3, src_data - 2, src_data - 1,
            src_data, src_data + 1, src_data + 2,
            effective_qp, strength, is_luma);

        src_data += src_width;
    }
}

void deblock_horizontal_edge(int16 *src_data, int32 src_width, uint8 average_qp, uint8 strength, bool is_luma, int16 qp_offset = 0)
{
    uint8 effective_qp = (uint8)evx_min2((int16)average_qp + qp_offset, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));

    for (uint8 i = 0; i < EVX_DEBLOCK_STEP_SIZE; i++)
    {
        int16 q0 = src_data[0 * src_width];
        int16 q1 = src_data[1 * src_width];
        int16 q2 = src_data[2 * src_width];
        int16 q3 = src_data[3 * src_width];

        int16 p0 = src_data[-1 * src_width];
        int16 p1 = src_data[-2 * src_width];
        int16 p2 = src_data[-3 * src_width];
        int16 p3 = src_data[-4 * src_width];

        deblock_filter_values(p3, p2, p1, p0, q0, q1, q2, q3,
            src_data - 3 * src_width, src_data - 2 * src_width, src_data - 1 * src_width,
            src_data, src_data + 1 * src_width, src_data + 2 * src_width,
            effective_qp, strength, is_luma);

        src_data++;
    }
}

int16 compute_vertical_boundary_strength(uint32 i, uint32 j, evx_block_desc *block_table, uint32 macroblock_size, uint32 width_in_blocks, int16 *out_avg_qp)
{
    uint32 left_block_index = deblock_macroblock_index(i - 1, j, macroblock_size, width_in_blocks);
    uint32 right_block_index = deblock_macroblock_index(i, j, macroblock_size, width_in_blocks);
    evx_block_desc *left_block_desc = &block_table[left_block_index];
    evx_block_desc *right_block_desc = &block_table[right_block_index];

    (*out_avg_qp) = compute_average_qp(*left_block_desc, *right_block_desc);

    return compute_deblock_strength((left_block_index != right_block_index), *left_block_desc, *right_block_desc);
}

int16 compute_horizontal_boundary_strength(uint32 i, uint32 j, evx_block_desc *block_table, uint32 macroblock_size, uint32 width_in_blocks, int16 *out_avg_qp)
{
    uint32 left_block_index = deblock_macroblock_index(i, j - 1, macroblock_size, width_in_blocks);
    uint32 right_block_index = deblock_macroblock_index(i, j, macroblock_size, width_in_blocks);
    evx_block_desc *left_block_desc = &block_table[left_block_index];
    evx_block_desc *right_block_desc = &block_table[right_block_index];

    (*out_avg_qp) = compute_average_qp(*left_block_desc, *right_block_desc);

    return compute_deblock_strength((left_block_index != right_block_index), *left_block_desc, *right_block_desc);
}

evx_status deblock_image(int16 macroblock_size, evx_block_desc *block_table, bool is_luma, image *target_image, int16 qp_offset = 0)
{
    int16 strength = 0;
    int16 average_qp = 0;

    uint32 width = target_image->query_width();
    uint32 height = target_image->query_height();
    uint32 width_in_blocks = width / macroblock_size;

    int16 *image_data = reinterpret_cast<int16 *>(target_image->query_data());

    for (uint32 i = EVX_DEBLOCK_STEP_SIZE; i < width; i += EVX_DEBLOCK_STEP_SIZE)
    {
        int16 strength = compute_vertical_boundary_strength(i, 0, block_table, macroblock_size, width_in_blocks, &average_qp);

        if (strength)
        {
            deblock_vertical_edge(image_data + i, width, average_qp, strength, is_luma, qp_offset);
        }
    }

    image_data += EVX_DEBLOCK_STEP_SIZE * width;

    for (uint32 j = EVX_DEBLOCK_STEP_SIZE; j < height; j += EVX_DEBLOCK_STEP_SIZE)
    {
        strength = compute_horizontal_boundary_strength(0, j, block_table, macroblock_size, width_in_blocks, &average_qp);

        if (strength)
        {
            deblock_horizontal_edge(image_data, width, average_qp, strength, is_luma, qp_offset);
        }

        for (uint32 i = EVX_DEBLOCK_STEP_SIZE; i < width; i += EVX_DEBLOCK_STEP_SIZE)
        {
            strength = compute_horizontal_boundary_strength(i, j, block_table, macroblock_size, width_in_blocks, &average_qp);

            if (strength)
            {
                deblock_horizontal_edge(image_data + i, width, average_qp, strength, is_luma, qp_offset);
            }

            strength = compute_vertical_boundary_strength(i, j, block_table, macroblock_size, width_in_blocks, &average_qp);

            if (strength)
            {
                deblock_vertical_edge(image_data + i, width, average_qp, strength, is_luma, qp_offset);
            }
        }

        image_data += EVX_DEBLOCK_STEP_SIZE * width;
    }

    return EVX_SUCCESS;
}

evx_status deblock_image_set(evx_block_desc *block_table, image_set *target_image_set)
{
    // Perform a deblocking operation on all channels of target_image_set.
    if (evx_failed(deblock_image(EVX_MACROBLOCK_SIZE, block_table, true, target_image_set->query_y_image(), 0)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(deblock_image(EVX_MACROBLOCK_SIZE >> 1, block_table, false, target_image_set->query_u_image(), EVX_CHROMA_QP_OFFSET)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (evx_failed(deblock_image(EVX_MACROBLOCK_SIZE >> 1, block_table, false, target_image_set->query_v_image(), EVX_CHROMA_QP_OFFSET)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return EVX_SUCCESS;
}

evx_status deblock_image_filter(evx_block_desc *block_table, image_set *target_image)
{
#if EVX_ENABLE_DEBLOCKING
    return deblock_image_set(block_table, target_image);
#else
    return EVX_SUCCESS;
#endif
}

} // namespace evx