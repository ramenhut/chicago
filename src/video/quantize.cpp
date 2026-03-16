
#include "quantize.h"
#include "analysis.h"
#include "config.h"

// The quantizer scale factor enables us to adjust the scale of the 
// quantization matrix. The default value is 16, which offers a reasonable
// tradeoff between quality and efficiency for most content.

#define EVX_QUANTIZER_SCALE_FACTOR                      (16)

namespace evx {

const int16 default_intra_8x8_qm[] =
{
     8, 10, 12, 15, 19, 23, 25, 27,
    10, 12, 14, 17, 21, 25, 27, 28,
    12, 14, 18, 21, 24, 26, 28, 30,
    15, 17, 21, 24, 26, 28, 30, 32,
    19, 21, 24, 26, 28, 30, 32, 35,
    23, 25, 26, 28, 30, 32, 35, 38,
    25, 27, 28, 30, 32, 35, 38, 41,
    27, 28, 30, 32, 35, 38, 41, 45
};

const int16 default_inter_8x8_qm[] =
{
    16, 17, 18, 19, 20, 21, 22, 23,
    17, 18, 19, 20, 21, 22, 23, 24,
    18, 19, 20, 21, 22, 23, 24, 25,
    19, 20, 21, 22, 23, 24, 26, 27,
    20, 21, 22, 23, 25, 26, 27, 28,
    21, 22, 23, 24, 26, 27, 28, 30,
    22, 23, 24, 26, 27, 28, 30, 31,
    23, 24, 25, 27, 28, 30, 31, 33
};

#if EVX_ENABLE_INTRA_DEADZONE
// Lighter deadzone weights for intra quantization (8x8). Weights 0-3,
// biased toward preserving more detail since intra has no reference fallback.
// DC position (0,0) = 0 means no deadzone on DC.
static const uint8 intra_deadzone_weight_8x8[] =
{
    0, 1, 1, 1, 1, 1, 2, 2,
    1, 1, 1, 1, 1, 2, 2, 2,
    1, 1, 1, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 2, 2, 3,
    1, 1, 2, 2, 2, 2, 3, 3,
    1, 2, 2, 2, 2, 3, 3, 3,
    2, 2, 2, 2, 3, 3, 3, 3,
    2, 2, 2, 3, 3, 3, 3, 3,
};
#endif

#if EVX_ENABLE_FREQ_DEADZONE
// Deadzone weights for inter quantization (8x8). At runtime, the weight is
// divided by 2 to produce the effective multiplier on the base deadzone:
//   weight 2 → 1.0x (unchanged), 3 → 1.5x, 4 → 2.0x.
// Higher weights at high-frequency positions zero out insignificant AC
// coefficients more aggressively, improving compression efficiency.
static const uint8 deadzone_weight_8x8[] =
{
    2, 2, 2, 3, 3, 3, 4, 4,
    2, 2, 3, 3, 3, 4, 4, 4,
    2, 3, 3, 3, 4, 4, 4, 4,
    3, 3, 3, 4, 4, 4, 4, 4,
    3, 3, 4, 4, 4, 4, 4, 4,
    3, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4,
};
#endif

int16 compute_luma_dc_scale(int16 qp)
{
    if (qp < 5)
        return evx_max2((int16)(qp << 1), (int16)2);
    else if (qp < 9)
        return qp << 1;
    else if (qp < 25)
        return qp + 8;
    return (qp << 1) - 16;
}

int16 compute_chroma_dc_scale(int16 qp)
{
    if (qp < 5)
        return evx_max2((int16)(qp << 1), (int16)2);
    else if (qp < 25)
        return (qp + 13) >> 1;
    return qp - 6;
}

// Adaptive quantization allows us to dynamically scale the quantization 
// parameter based on the statistical characteristics of the incoming block.

uint8 query_block_quantization_parameter(uint8 quality, const macroblock &src, EVX_BLOCK_TYPE block_type)
{
#if EVX_QUANTIZATION_ENABLED
  #if EVX_ADAPTIVE_QUANTIZATION
    uint32 variance = compute_block_variance2(src);
    uint8 index = clip_range(log2(variance) >> 1, 1, EVX_MAX_MPEG_QUANT_LEVELS - 1);

    // Adapt QP toward block variance, but cap the adjustment at ±6 so the
    // user's quality parameter is respected at high values (prevents the
    // effective QP range from collapsing when quality > ~20).
    int16 delta = ((int16)index - (int16)quality) >> 1;
    if (delta > 6) delta = 6;
    if (delta < -6) delta = -6;

    return clip_range((int16)quality + delta, (int16)1, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));
  #else
    return quality;
  #endif
#else
    return 0;
#endif
}

void quantize_luma_intra_block_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    // Quantize our luminance values.
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {
        int16 qm_value = default_intra_8x8_qm[k + j * 8];
        int16 source_luma = source[k + j * source_stride];

#if EVX_ROUNDED_QUANTIZATION
        int16 qfactor = rounded_div(source_luma * EVX_QUANTIZER_SCALE_FACTOR, qm_value);
#else
        int16 qfactor = (source_luma * EVX_QUANTIZER_SCALE_FACTOR) / qm_value;
#endif

#if EVX_ENABLE_INTRA_DEADZONE
        if (qp >= 16 && !(k == 0 && j == 0))
        {
            int16 weight = intra_deadzone_weight_8x8[k + j * 8];
            int16 dz = (sign(qfactor) * qp * weight) >> 1;
            qfactor = qfactor - dz;
        }
#endif

#if EVX_ROUNDED_QUANTIZATION
        dest[k + j * dest_stride] = rounded_div(qfactor, qp << 1);
#else
        dest[k + j * dest_stride] = qfactor / (qp << 1);
#endif
    }

    // For intra matrices we weight the dc coefficient separately.
    int16 luma_dc_scale = compute_luma_dc_scale(qp);

#if EVX_ROUNDED_QUANTIZATION
    dest[0] = rounded_div(source[0], luma_dc_scale);
#else
    dest[0] = (source[0] / luma_dc_scale);
#endif
}

void quantize_chroma_intra_block_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    // Quantize our chrominance values.
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {
        int16 qm_value = default_intra_8x8_qm[k + j * 8];
        int16 source_chroma = source[k + j * source_stride];

#if EVX_ROUNDED_QUANTIZATION
        int16 qfactor = rounded_div(source_chroma * EVX_QUANTIZER_SCALE_FACTOR, qm_value);
#else
        int16 qfactor = (source_chroma * EVX_QUANTIZER_SCALE_FACTOR) / qm_value;
#endif

#if EVX_ENABLE_INTRA_DEADZONE
        if (qp >= 16 && !(k == 0 && j == 0))
        {
            int16 weight = intra_deadzone_weight_8x8[k + j * 8];
            int16 dz = (sign(qfactor) * qp * weight) >> 1;
            qfactor = qfactor - dz;
        }
#endif

#if EVX_ROUNDED_QUANTIZATION
        dest[k + j * dest_stride] = rounded_div(qfactor, qp << 1);
#else
        dest[k + j * dest_stride] = qfactor / (qp << 1);
#endif
    }

    // For intra matrices we weight the dc coefficient separately.
    int16 chroma_dc_scale = compute_chroma_dc_scale(qp);

#if EVX_ROUNDED_QUANTIZATION
    dest[0] = rounded_div(source[0], chroma_dc_scale);
#else
    dest[0] = (source[0] / chroma_dc_scale);
#endif
}

void quantize_intra_block_linear_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {   
        int16 source_value = source[k + j * source_stride];

#if EVX_ROUNDED_QUANTIZATION
        dest[k + j * dest_stride] = rounded_div(source_value, qp << 1);
#else
        dest[k + j * dest_stride] = (source_value) / (qp << 1);
#endif
    }
}

void quantize_inter_block_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    // Quantize our inter values.
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {
        int16 qm_value = default_inter_8x8_qm[k + j * 8];
        int16 source_value = source[k + j * source_stride];

#if EVX_ROUNDED_QUANTIZATION
        int16 qfactor = rounded_div(source_value * EVX_QUANTIZER_SCALE_FACTOR, qm_value);
  #if EVX_ENABLE_FREQ_DEADZONE
        int16 weight = deadzone_weight_8x8[k + j * 8];
    #if EVX_ENABLE_QP_ADAPTIVE_DEADZONE
        if (qp >= 16) weight = evx_min2((int16)(weight + 1), (int16)6);
    #endif
        int16 dz = (sign(qfactor) * qp * weight) >> 1;
  #else
        int16 dz = sign(qfactor) * qp;
  #endif
        dest[k + j * dest_stride] = rounded_div(qfactor - dz, qp << 1);
#else
        int16 qfactor = (source_value * EVX_QUANTIZER_SCALE_FACTOR) / qm_value;
  #if EVX_ENABLE_FREQ_DEADZONE
        int16 weight = deadzone_weight_8x8[k + j * 8];
    #if EVX_ENABLE_QP_ADAPTIVE_DEADZONE
        if (qp >= 16) weight = evx_min2((int16)(weight + 1), (int16)6);
    #endif
        int16 dz = (sign(qfactor) * qp * weight) >> 1;
  #else
        int16 dz = sign(qfactor) * qp;
  #endif
        dest[k + j * dest_stride] = (qfactor - dz) / (qp << 1);
#endif
    }
}

void quantize_inter_block_linear_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {   
        int16 source_value = source[k + j * source_stride];
        int16 qm_value = (abs(source_value) - (qp >> 1));

#if EVX_ROUNDED_QUANTIZATION
        dest[k + j * dest_stride] = rounded_div(qm_value, qp << 1);
#else
        dest[k + j * dest_stride] = (qm_value) / (qp << 1);
#endif
        dest[k + j * dest_stride] *= sign(source_value);
    }
}

void inverse_quantize_luma_intra_block_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    // Inverse quantize our luminance values.
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {   
        int16 qm_value = default_intra_8x8_qm[k + j * 8];
        int16 source_luma = source[k + j * source_stride];
        dest[k + j * dest_stride] = (2 * source_luma * qm_value * qp) / EVX_QUANTIZER_SCALE_FACTOR;
    }

    // For intra matrices we weight the dc coefficient separately.
    int16 luma_dc_scale = compute_luma_dc_scale(qp);
    dest[0] = source[0] * luma_dc_scale;
}

void inverse_quantize_chroma_intra_block_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    // Inverse quantize our chrominance values.
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {   
        int16 qm_value = default_intra_8x8_qm[k + j * 8];
        int16 source_chroma = source[k + j * source_stride];
        dest[k + j * dest_stride] = (2 * source_chroma * qm_value * qp) / EVX_QUANTIZER_SCALE_FACTOR;
    }

    // For intra matrices we weight the dc coefficient separately.
    int16 chroma_dc_scale = compute_chroma_dc_scale(qp);
    dest[0] = source[0] * chroma_dc_scale;
}

void inverse_quantize_block_linear_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {   
        int16 source_value = source[k + j * source_stride];
        dest[k + j * dest_stride] = 0;

        if (source_value)
        {
            int16 mod_qp = (qp + 1) % 2;
            int16 qm_value = (abs(source_value) << 1) + 1;
            
            dest[k + j * dest_stride] = qm_value * qp - 1 * mod_qp;
            dest[k + j * dest_stride] *= sign(source_value);
        }
    }
}

void inverse_quantize_inter_block_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    // Inverse quantize our inter values.
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {   
        int16 qm_value = default_inter_8x8_qm[k + j * 8];
        int16 source_value = source[k + j * source_stride];
        dest[k + j * dest_stride] = ((2 * source_value) * qm_value * qp) / EVX_QUANTIZER_SCALE_FACTOR; 
    }
}

void inverse_quantize_block_flat_8x8(uint8 qp, int16 *source, int32 source_stride, int16 *dest, int32 dest_stride)
{
    // Inverse quantize our inter values.
    for (uint32 j = 0; j < 8; ++j)
    for (uint32 k = 0; k < 8; ++k)
    {   
        int16 source_value = source[k + j * source_stride];
        dest[k + j * dest_stride] = source_value * qp; 
    }
}

void quantize_intra_macroblock(uint8 qp, const macroblock &source, macroblock *dest)
{
    uint8 chroma_qp = (uint8)evx_min2((int16)qp + EVX_CHROMA_QP_OFFSET, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));

#if EVX_ENABLE_LINEAR_QUANTIZATION
    // Luminance blocks.
    quantize_intra_block_linear_8x8(qp, source.data_y, source.stride, dest->data_y, dest->stride);
    quantize_intra_block_linear_8x8(qp, source.data_y + 8, source.stride, dest->data_y + 8, dest->stride);
    quantize_intra_block_linear_8x8(qp, source.data_y + 8 * source.stride, source.stride, dest->data_y + 8 * dest->stride, dest->stride);
    quantize_intra_block_linear_8x8(qp, source.data_y + 8 * source.stride + 8, source.stride, dest->data_y + 8 * dest->stride + 8, dest->stride);

    // Chroma blocks (with QP offset for perceptual optimization).
    quantize_intra_block_linear_8x8(chroma_qp, source.data_u, source.stride >> 1, dest->data_u, dest->stride >> 1);
    quantize_intra_block_linear_8x8(chroma_qp, source.data_v, source.stride >> 1, dest->data_v, dest->stride >> 1);
#else
    // Luminance blocks.
    quantize_luma_intra_block_8x8(qp, source.data_y, source.stride, dest->data_y, dest->stride);
    quantize_luma_intra_block_8x8(qp, source.data_y + 8, source.stride, dest->data_y + 8, dest->stride);
    quantize_luma_intra_block_8x8(qp, source.data_y + 8 * source.stride, source.stride, dest->data_y + 8 * dest->stride, dest->stride);
    quantize_luma_intra_block_8x8(qp, source.data_y + 8 * source.stride + 8, source.stride, dest->data_y + 8 * dest->stride + 8, dest->stride);

    // Chroma blocks (with QP offset for perceptual optimization).
    quantize_chroma_intra_block_8x8(chroma_qp, source.data_u, source.stride >> 1, dest->data_u, dest->stride >> 1);
    quantize_chroma_intra_block_8x8(chroma_qp, source.data_v, source.stride >> 1, dest->data_v, dest->stride >> 1);
#endif
}

void quantize_inter_macroblock(uint8 qp, const macroblock &source, macroblock *dest)
{
    uint8 chroma_qp = (uint8)evx_min2((int16)qp + EVX_CHROMA_QP_OFFSET, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));

#if EVX_ENABLE_LINEAR_QUANTIZATION
    // Luminance blocks.
    quantize_inter_block_linear_8x8(qp, source.data_y, source.stride, dest->data_y, dest->stride);
    quantize_inter_block_linear_8x8(qp, source.data_y + 8, source.stride, dest->data_y + 8, dest->stride);
    quantize_inter_block_linear_8x8(qp, source.data_y + 8 * source.stride, source.stride, dest->data_y + 8 * dest->stride, dest->stride);
    quantize_inter_block_linear_8x8(qp, source.data_y + 8 * source.stride + 8, source.stride, dest->data_y + 8 * dest->stride + 8, dest->stride);

    // Chroma blocks (with QP offset for perceptual optimization).
    quantize_inter_block_linear_8x8(chroma_qp, source.data_u, source.stride >> 1, dest->data_u, dest->stride >> 1);
    quantize_inter_block_linear_8x8(chroma_qp, source.data_v, source.stride >> 1, dest->data_v, dest->stride >> 1);
#else
    // Luminance blocks.
    quantize_inter_block_8x8(qp, source.data_y, source.stride, dest->data_y, dest->stride);
    quantize_inter_block_8x8(qp, source.data_y + 8, source.stride, dest->data_y + 8, dest->stride);
    quantize_inter_block_8x8(qp, source.data_y + 8 * source.stride, source.stride, dest->data_y + 8 * dest->stride, dest->stride);
    quantize_inter_block_8x8(qp, source.data_y + 8 * source.stride + 8, source.stride, dest->data_y + 8 * dest->stride + 8, dest->stride);

    // Chroma blocks (with QP offset for perceptual optimization).
    quantize_inter_block_8x8(chroma_qp, source.data_u, source.stride >> 1, dest->data_u, dest->stride >> 1);
    quantize_inter_block_8x8(chroma_qp, source.data_v, source.stride >> 1, dest->data_v, dest->stride >> 1);
#endif
}   

void inverse_quantize_intra_macroblock(uint8 qp, const macroblock &source, macroblock *dest)
{
    uint8 chroma_qp = (uint8)evx_min2((int16)qp + EVX_CHROMA_QP_OFFSET, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));

#if EVX_ENABLE_LINEAR_QUANTIZATION
    // Luminance blocks.
    inverse_quantize_block_linear_8x8(qp, source.data_y, source.stride, dest->data_y, dest->stride);
    inverse_quantize_block_linear_8x8(qp, source.data_y + 8, source.stride, dest->data_y + 8, dest->stride);
    inverse_quantize_block_linear_8x8(qp, source.data_y + 8 * source.stride, source.stride, dest->data_y + 8 * dest->stride, dest->stride);
    inverse_quantize_block_linear_8x8(qp, source.data_y + 8 * source.stride + 8, source.stride, dest->data_y + 8 * dest->stride + 8, dest->stride);

    // Chroma blocks (with QP offset matching forward quantization).
    inverse_quantize_block_linear_8x8(chroma_qp, source.data_u, source.stride >> 1, dest->data_u, dest->stride >> 1);
    inverse_quantize_block_linear_8x8(chroma_qp, source.data_v, source.stride >> 1, dest->data_v, dest->stride >> 1);
#else
    // Luminance blocks.
    inverse_quantize_luma_intra_block_8x8(qp, source.data_y, source.stride, dest->data_y, dest->stride);
    inverse_quantize_luma_intra_block_8x8(qp, source.data_y + 8, source.stride, dest->data_y + 8, dest->stride);
    inverse_quantize_luma_intra_block_8x8(qp, source.data_y + 8 * source.stride, source.stride, dest->data_y + 8 * dest->stride, dest->stride);
    inverse_quantize_luma_intra_block_8x8(qp, source.data_y + 8 * source.stride + 8, source.stride, dest->data_y + 8 * dest->stride + 8, dest->stride);

    // Chroma blocks (with QP offset matching forward quantization).
    inverse_quantize_chroma_intra_block_8x8(chroma_qp, source.data_u, source.stride >> 1, dest->data_u, dest->stride >> 1);
    inverse_quantize_chroma_intra_block_8x8(chroma_qp, source.data_v, source.stride >> 1, dest->data_v, dest->stride >> 1);
#endif
}

void inverse_quantize_inter_macroblock(uint8 qp, const macroblock &source, macroblock *dest)
{
    uint8 chroma_qp = (uint8)evx_min2((int16)qp + EVX_CHROMA_QP_OFFSET, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));

#if EVX_ENABLE_LINEAR_QUANTIZATION
    // Luminance blocks.
    inverse_quantize_block_linear_8x8(qp, source.data_y, source.stride, dest->data_y, dest->stride);
    inverse_quantize_block_linear_8x8(qp, source.data_y + 8, source.stride, dest->data_y + 8, dest->stride);
    inverse_quantize_block_linear_8x8(qp, source.data_y + 8 * source.stride, source.stride, dest->data_y + 8 * dest->stride, dest->stride);
    inverse_quantize_block_linear_8x8(qp, source.data_y + 8 * source.stride + 8, source.stride, dest->data_y + 8 * dest->stride + 8, dest->stride);

    // Chroma blocks (with QP offset matching forward quantization).
    inverse_quantize_block_linear_8x8(chroma_qp, source.data_u, source.stride >> 1, dest->data_u, dest->stride >> 1);
    inverse_quantize_block_linear_8x8(chroma_qp, source.data_v, source.stride >> 1, dest->data_v, dest->stride >> 1);
#else
    // Luminance blocks.
    inverse_quantize_inter_block_8x8(qp, source.data_y, source.stride, dest->data_y, dest->stride);
    inverse_quantize_inter_block_8x8(qp, source.data_y + 8, source.stride, dest->data_y + 8, dest->stride);
    inverse_quantize_inter_block_8x8(qp, source.data_y + 8 * source.stride, source.stride, dest->data_y + 8 * dest->stride, dest->stride);
    inverse_quantize_inter_block_8x8(qp, source.data_y + 8 * source.stride + 8, source.stride, dest->data_y + 8 * dest->stride + 8, dest->stride);

    // Chroma blocks (with QP offset matching forward quantization).
    inverse_quantize_inter_block_8x8(chroma_qp, source.data_u, source.stride >> 1, dest->data_u, dest->stride >> 1);
    inverse_quantize_inter_block_8x8(chroma_qp, source.data_v, source.stride >> 1, dest->data_v, dest->stride >> 1);
#endif
}

// Modified MPEG-2 quantization with adaptive qp.
void quantize_macroblock(uint8 qp, EVX_BLOCK_TYPE block_type, const macroblock &source, macroblock *__restrict dest)
{
#if EVX_QUANTIZATION_ENABLED
    if (EVX_IS_INTRA_BLOCK_TYPE(block_type) && !EVX_IS_MOTION_BLOCK_TYPE(block_type))
        return quantize_intra_macroblock(qp, source, dest);
    else
        return quantize_inter_macroblock(qp, source, dest);
#else
    copy_macroblock(source, dest);
#endif
}

void inverse_quantize_macroblock(uint8 qp, EVX_BLOCK_TYPE block_type, const macroblock &source, macroblock *__restrict dest)
{
#if EVX_QUANTIZATION_ENABLED
    if (EVX_IS_INTRA_BLOCK_TYPE(block_type) && !EVX_IS_MOTION_BLOCK_TYPE(block_type))
        return inverse_quantize_intra_macroblock(qp, source, dest);
    else
        return inverse_quantize_inter_macroblock(qp, source, dest);
#else
    copy_macroblock(source, dest);
#endif
}

#if EVX_ENABLE_TRELLIS_QUANTIZATION

// Reconstruct a single coefficient from its quantized level, matching the decoder exactly.
static int16 inverse_quantize_coeff(int16 level, uint8 qp, uint32 row, uint32 col,
                                     bool is_intra, bool is_chroma)
{
    if (level == 0) return 0;

#if EVX_ENABLE_LINEAR_QUANTIZATION
    if (is_intra)
    {
        return level * (qp << 1);
    }
    else
    {
        int16 mod_qp = (qp + 1) % 2;
        int16 qm_value = (abs(level) << 1) + 1;
        return sign(level) * (qm_value * qp - mod_qp);
    }
#else
    if (is_intra)
    {
        if (row == 0 && col == 0)
        {
            int16 dc_scale = is_chroma ? compute_chroma_dc_scale(qp) : compute_luma_dc_scale(qp);
            return level * dc_scale;
        }
        int16 qm_value = default_intra_8x8_qm[col + row * 8];
        return (2 * level * qm_value * qp) / EVX_QUANTIZER_SCALE_FACTOR;
    }
    else
    {
        int16 qm_value = default_inter_8x8_qm[col + row * 8];
        return (2 * level * qm_value * qp) / EVX_QUANTIZER_SCALE_FACTOR;
    }
#endif
}

// Estimate bit cost for a single coefficient level.
static uint32 estimate_coeff_bits(int16 level)
{
    if (level == 0) return 1;

    uint16 abs_val = (uint16)evx_min2(abs(level), (int16)255);

#if EVX_ENABLE_SIGMAP_CODING
    uint32 bits = 2; // sig_flag + sign
    if (abs_val > 1) bits += 1; // gt1
    if (abs_val > 2) bits += 1 + 2 * evx_required_bits((uint8)(abs_val - 3 + 1)); // gt2 + remainder
    else bits += 1; // gt1 flag alone
    return bits;
#else
    return 2 * evx_required_bits((uint8)abs_val) + 2;
#endif
}

// Core trellis optimization for a single contiguous 8x8 block.
// Two passes: (1) tail trimming, (2) forward coefficient refinement.
static void trellis_optimize_block_8x8(uint8 qp, bool is_intra, bool is_chroma,
    const int16 *transform_coeffs, const uint8 *scan_table,
    float lambda, int16 *quantized_levels)
{
    // Find last significant position in scan order
    int32 last_sig = -1;
    for (int32 pos = 63; pos >= 0; pos--)
    {
        if (quantized_levels[scan_table[pos]] != 0)
        {
            last_sig = pos;
            break;
        }
    }

    if (last_sig < 0) return; // All zeros, nothing to optimize

    // Pass 1: Tail trimming — zero trailing coefficients where rate savings outweigh distortion cost
    for (int32 pos = last_sig; pos >= 1; pos--)
    {
        uint32 offset = scan_table[pos];
        int16 level = quantized_levels[offset];
        if (level == 0) continue;

        uint32 row = offset >> 3;
        uint32 col = offset & 7;

        int16 recon = inverse_quantize_coeff(level, qp, row, col, is_intra, is_chroma);
        int32 diff_keep = (int32)transform_coeffs[offset] - (int32)recon;
        int32 diff_zero = (int32)transform_coeffs[offset];

        float cost_keep = (float)(diff_keep * diff_keep) + lambda * (float)estimate_coeff_bits(level);
        float cost_zero = (float)(diff_zero * diff_zero) + lambda * (float)estimate_coeff_bits(0);

        if (cost_zero <= cost_keep)
            quantized_levels[offset] = 0;
        else
            break; // Stop trimming once a coefficient is worth keeping
    }

    // Recompute last_sig after trimming
    last_sig = -1;
    for (int32 pos = 63; pos >= 0; pos--)
    {
        if (quantized_levels[scan_table[pos]] != 0)
        {
            last_sig = pos;
            break;
        }
    }

    if (last_sig < 0) return;

    // Pass 2: Forward refinement — reduce magnitude by 1 where R-D cost improves. Skip DC.
    for (int32 pos = 1; pos <= last_sig; pos++)
    {
        uint32 offset = scan_table[pos];
        int16 level = quantized_levels[offset];
        if (level == 0) continue;

        uint32 row = offset >> 3;
        uint32 col = offset & 7;

        int16 reduced = level - sign(level);

        int16 recon_keep = inverse_quantize_coeff(level, qp, row, col, is_intra, is_chroma);
        int16 recon_reduced = inverse_quantize_coeff(reduced, qp, row, col, is_intra, is_chroma);

        int32 diff_keep = (int32)transform_coeffs[offset] - (int32)recon_keep;
        int32 diff_reduced = (int32)transform_coeffs[offset] - (int32)recon_reduced;

        float cost_keep = (float)(diff_keep * diff_keep) + lambda * (float)estimate_coeff_bits(level);
        float cost_reduced = (float)(diff_reduced * diff_reduced) + lambda * (float)estimate_coeff_bits(reduced);

        if (cost_reduced < cost_keep)
            quantized_levels[offset] = reduced;
    }
}

void trellis_optimize_macroblock(uint8 qp, EVX_BLOCK_TYPE block_type,
    const macroblock &transform_coeffs, const uint8 *scan_table,
    float lambda, macroblock *quantized_block)
{
    bool is_intra = EVX_IS_INTRA_BLOCK_TYPE(block_type) && !EVX_IS_MOTION_BLOCK_TYPE(block_type);
    uint8 chroma_qp = (uint8)evx_min2((int16)qp + EVX_CHROMA_QP_OFFSET, (int16)(EVX_MAX_MPEG_QUANT_LEVELS - 1));

    int16 trans_buf[64], quant_buf[64];

    // Process 4 luma 8x8 sub-blocks
    const int32 luma_ox[] = {0, 8, 0, 8};
    const int32 luma_oy[] = {0, 0, 8, 8};

    for (int32 sb = 0; sb < 4; sb++)
    {
        int32 ox = luma_ox[sb];
        int32 oy = luma_oy[sb];

        for (int32 j = 0; j < 8; j++)
        for (int32 k = 0; k < 8; k++)
        {
            trans_buf[k + j * 8] = transform_coeffs.data_y[(oy + j) * transform_coeffs.stride + (ox + k)];
            quant_buf[k + j * 8] = quantized_block->data_y[(oy + j) * quantized_block->stride + (ox + k)];
        }

        trellis_optimize_block_8x8(qp, is_intra, false, trans_buf, scan_table, lambda, quant_buf);

        for (int32 j = 0; j < 8; j++)
        for (int32 k = 0; k < 8; k++)
            quantized_block->data_y[(oy + j) * quantized_block->stride + (ox + k)] = quant_buf[k + j * 8];
    }

#if EVX_ENABLE_CHROMA_SUPPORT
    // Process U chroma 8x8 block
    uint32 chroma_stride_t = transform_coeffs.stride >> 1;
    uint32 chroma_stride_q = quantized_block->stride >> 1;

    for (int32 j = 0; j < 8; j++)
    for (int32 k = 0; k < 8; k++)
    {
        trans_buf[k + j * 8] = transform_coeffs.data_u[j * chroma_stride_t + k];
        quant_buf[k + j * 8] = quantized_block->data_u[j * chroma_stride_q + k];
    }

    trellis_optimize_block_8x8(chroma_qp, is_intra, true, trans_buf, scan_table, lambda, quant_buf);

    for (int32 j = 0; j < 8; j++)
    for (int32 k = 0; k < 8; k++)
        quantized_block->data_u[j * chroma_stride_q + k] = quant_buf[k + j * 8];

    // Process V chroma 8x8 block
    for (int32 j = 0; j < 8; j++)
    for (int32 k = 0; k < 8; k++)
    {
        trans_buf[k + j * 8] = transform_coeffs.data_v[j * chroma_stride_t + k];
        quant_buf[k + j * 8] = quantized_block->data_v[j * chroma_stride_q + k];
    }

    trellis_optimize_block_8x8(chroma_qp, is_intra, true, trans_buf, scan_table, lambda, quant_buf);

    for (int32 j = 0; j < 8; j++)
    for (int32 k = 0; k < 8; k++)
        quantized_block->data_v[j * chroma_stride_q + k] = quant_buf[k + j * 8];
#endif
}

#endif // EVX_ENABLE_TRELLIS_QUANTIZATION

} // namespace evx