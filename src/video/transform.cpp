
#include "transform.h"
#include "evx_math.h"
#include "xftables.h"

namespace evx {

// There are several far more efficient implementations of this transform. We've chosen
// not to implement them here for the sake of clarity, but interested readers should 
// consult Chen'77 and Loeffer'89.

#define EVX_TRANSFORM_SUB_LINE(n)         \
    for (uint8 i = 0; i < n; ++i)         \
    {                                     \
        output[i] = left[i] - right[i];   \
    }

inline void sub_4x4_line(int16 *left, int16 *right, int16 *output)
{
    EVX_TRANSFORM_SUB_LINE(4);
}

inline void sub_8x8_line(int16 *left, int16 *right, int16 *output)
{
    EVX_TRANSFORM_SUB_LINE(8);
}

inline void sub_16x16_line(int16 *left, int16 *right, int16 *output)
{
    EVX_TRANSFORM_SUB_LINE(16);
}

// Note: shift operators are used sparingly here, to avoid undefined behavior when
//       shifting negative transform values.

void transform_4x4_line(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 4; ++i)
    {
        int32 total = 0;             
        int16 *output = dest + i * dest_pitch;
 
        for (uint8 k = 0; k < 4; ++k)
        {
            int16 *input = src + k * src_pitch;
            total += *input * EVX_TRANSFORM_4x4_TRIG_128_LUT[i * 4 + k];
        }

        if (0 == i)
            // mul by sqrt(1 / 4)  
            total = (total >> 1);      
        else
            // mul by sqrt(2 / 4)
            total = (total * 90) / 128;
 
        total = rounded_div(total, 128);
        *output = total;
    }
}

void transform_4x4_line_fast(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 4; ++i)
    {
        int32 total = 0;             
        int16 *output = dest + i * dest_pitch;

        total  = src[0 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[i * 4 + 0];
        total += src[1 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[i * 4 + 1];
        total += src[2 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[i * 4 + 2];
        total += src[3 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[i * 4 + 3];

        total = (!i) * (total >> 1) + (!!i) * ((total * 2896) >> 12);
        total = rounded_div(total, 128);
        *output = total;
    }
}

void transform_4x4(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 scratch_block[4*4];

    // Horizontal DCT-II
    for (uint8 j = 0; j < 4; ++j)
    {
        transform_4x4_line_fast(src + j * src_pitch, 1, scratch_block + j * 4, 1);
    }
    
    // Vertical DCT-II
    for (uint8 j = 0; j < 4; ++j)
    {
        transform_4x4_line_fast(scratch_block + j, 4, dest + j, dest_pitch);
    }
}

void inverse_transform_4x4_line(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 4; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;
 
        for (uint8 k = 0; k < 4; ++k)
        {
            int16 *input = src + k * src_pitch;
            int32 temp_total = *input * EVX_TRANSFORM_4x4_TRIG_128_LUT[k * 4 + i];

            if (0 == k)
                // mul by sqrt(1 / 4)
                temp_total = (temp_total >> 1);
            else
                // mul by sqrt(2 / 4)
                temp_total = ((temp_total * 90) / 128);
                
            total += temp_total;
        }

        total = rounded_div(total, 128);
        *output = total;
    }
}

void inverse_transform_4x4_line_fast(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 4; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;

        total =   (src[0 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[0 * 4 + i] ) >> 1;
        total += ((src[1 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[1 * 4 + i] ) * 2896) >> 12;
        total += ((src[2 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[2 * 4 + i] ) * 2896) >> 12;
        total += ((src[3 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[3 * 4 + i] ) * 2896) >> 12;

        total = rounded_div(total, 128);
        *output = total;
    }
}

void inverse_transform_4x4(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 scratch_block[4*4];

    // Vertical IDCT-II
    for (uint8 j = 0; j < 4; ++j)
    {
        inverse_transform_4x4_line_fast(src + j, src_pitch, scratch_block + j, 4);
    }

    // Horizontal IDCT-II
    for (uint8 j = 0; j < 4; ++j)
    {
        inverse_transform_4x4_line_fast(scratch_block + j * 4, 1, dest + j * dest_pitch, 1);
    }
}

void inverse_transform_add_4x4_line(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 4; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;
        int16 *add_input = add + i * add_pitch;
 
        for (uint8 k = 0; k < 4; ++k)
        {
            int16 *input = src + k * src_pitch;
            int32 temp_total = *input * EVX_TRANSFORM_4x4_TRIG_128_LUT[k * 4 + i];

            if (0 == k)
                // mul by sqrt(1 / 4)
                temp_total = (temp_total >> 1);                
            else		  
                // mul by sqrt(2 / 4)
                temp_total = ((temp_total * 90) / 128);
                
            total += temp_total;
        }

        total = rounded_div(total, 128);
        *output = total + *add_input;
    }
}

void inverse_transform_add_4x4_line_fast(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 4; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;
        int16 *add_input = add + i * add_pitch;

        total =   (src[0 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[0 * 4 + i] ) >> 1;
        total += ((src[1 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[1 * 4 + i] ) * 2896) >> 12;
        total += ((src[2 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[2 * 4 + i] ) * 2896) >> 12;
        total += ((src[3 * src_pitch] * EVX_TRANSFORM_4x4_TRIG_128_LUT[3 * 4 + i] ) * 2896) >> 12;

        total = rounded_div(total, 128);
        *output = total + *add_input;
    }
}

void inverse_transform_add_4x4(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 scratch_block[4*4];

    // Vertical IDCT-II
    for (uint8 j = 0; j < 4; ++j)
    {
        inverse_transform_4x4_line_fast(src + j, src_pitch, scratch_block + j, 4);
    }

    // Horizontal IDCT-II
    for (uint8 j = 0; j < 4; j++)
    {
        inverse_transform_add_4x4_line_fast(scratch_block + j * 4, 1, add + j * add_pitch, 1, dest + j * dest_pitch, 1 );
    }
}

void sub_transform_4x4(int16 *src, uint32 src_pitch, int16 *sub, uint32 sub_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 scratch_block[4*4];
    int16 sub_scratch_block[4*4];

    // Horizontal IDCT-II
    for (uint8 j = 0; j < 4; j++) 
    {
        sub_4x4_line(src + j * src_pitch, sub + j * sub_pitch, sub_scratch_block + j * 4 );
        transform_4x4_line_fast(sub_scratch_block + j * 4, 1, scratch_block + j * 4, 1 );
    }
    
    // Vertical IDCT-II
    for (uint8 j = 0; j < 4; j++)
    {
        transform_4x4_line_fast(scratch_block + j, 4, dest + j, dest_pitch);
    }
}

void transform_8x8_line(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 8; ++i)
    {
        int32 total = 0;             
        int16 *output = dest + i * dest_pitch;
 
        for (uint8 k = 0; k < 8; ++k)
        {
            int16 *input = src + k * src_pitch;
            total += *input * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + k];
        }

        if (0 == i) 
            // mul by sqrt(1 / 8)
            total = (total * 45) / 128; 
        else
            // mul by sqrt(2 / 8)
            total = (total / 2);
 
        total = rounded_div(total, 128);
        *output = total;
    }
}

void transform_8x8_line_fast(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 8; ++i)
    {
        int32 total = 0;             
        int16 *output = dest + i * dest_pitch;

        total  = src[0 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + 0];
        total += src[1 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + 1];
        total += src[2 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + 2];
        total += src[3 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + 3];
        total += src[4 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + 4];
        total += src[5 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + 5];
        total += src[6 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + 6];
        total += src[7 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[i * 8 + 7];

        total = (!i) * ((total * 45) / 128) + (!!i) * (total / 2);
        total = rounded_div(total, 128);
        *output = total;
    }
}

void transform_8x8(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 scratch_block[8*8];

    // Horizontal DCT-II
    for (uint8 j = 0; j < 8; ++j)
    {
        transform_8x8_line_fast(src + j * src_pitch, 1, scratch_block + j * 8, 1);
    }
    
    // Vertical DCT-II
    for (uint8 j = 0; j < 8; ++j)
    {
        transform_8x8_line_fast(scratch_block + j, 8, dest + j, dest_pitch);
    }
}

void inverse_transform_8x8_line(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 8; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;
 
        for (uint8 k = 0; k < 8; ++k)
        {
            int16 *input = src + k * src_pitch;
            int32 temp_total = *input * EVX_TRANSFORM_8x8_TRIG_128_LUT[k * 8 + i];

            if (0 == k)
                // mul by sqrt(1 / 8)
                temp_total = (temp_total * 45) / 128;	
            else
                // imul by sqrt(2 / 8)
                temp_total = (temp_total / 2);			    
                
            total += temp_total;
        }

        total = rounded_div(total, 128);
        *output = total;
    }
}

void inverse_transform_8x8_line_fast(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 8; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;

        total = ((src[0 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[0 * 8 + i]) * 45) / 128;
        total += (src[1 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[1 * 8 + i]) / 2;
        total += (src[2 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[2 * 8 + i]) / 2;
        total += (src[3 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[3 * 8 + i]) / 2;
        total += (src[4 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[4 * 8 + i]) / 2;
        total += (src[5 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[5 * 8 + i]) / 2;
        total += (src[6 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[6 * 8 + i]) / 2;
        total += (src[7 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[7 * 8 + i]) / 2;

        total = rounded_div(total, 128);
        *output = total;
    }
}

void inverse_transform_8x8(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 scratch_block[8*8];

    // Vertical IDCT-II
    for (uint8 j = 0; j < 8; ++j)
    {
        inverse_transform_8x8_line_fast(src + j, src_pitch, scratch_block + j, 8);
    }

    // Horizontal IDCT-II
    for (uint8 j = 0; j < 8; ++j)
    {
        inverse_transform_8x8_line_fast(scratch_block + j * 8, 1, dest + j * dest_pitch, 1);
    }
}

void inverse_transform_add_8x8_line(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 8; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;
        int16 *add_input = add + i * add_pitch;
 
        for (uint8 k = 0; k < 8; ++k)
        {
            int16 *input = src + k * src_pitch;
            int32 temp_total = *input * EVX_TRANSFORM_8x8_TRIG_128_LUT[k * 8 + i];

            if ( 0 == k )
                // mul by sqrt(1 / length)
                temp_total = (temp_total * 45) / 128;
            else
                // mul by sqrt(2 / length)
                temp_total = (temp_total / 2);			   
                
            total += temp_total;
        }

        total = rounded_div(total, 128);
        *output = total + *add_input;
    }
}

void inverse_transform_add_8x8_line_fast(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 8; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;
        int16 *add_input = add + i * add_pitch;

        total = ((src[ 0 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[0 * 8 + i] ) * 45) / 128;
        total += (src[ 1 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[1 * 8 + i] ) / 2;
        total += (src[ 2 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[2 * 8 + i] ) / 2;
        total += (src[ 3 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[3 * 8 + i] ) / 2;
        total += (src[ 4 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[4 * 8 + i] ) / 2;
        total += (src[ 5 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[5 * 8 + i] ) / 2;
        total += (src[ 6 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[6 * 8 + i] ) / 2;
        total += (src[ 7 * src_pitch] * EVX_TRANSFORM_8x8_TRIG_128_LUT[7 * 8 + i] ) / 2;

        total = rounded_div(total, 128);
        *output = total + *add_input;
    }
}

void inverse_transform_add_8x8(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 scratch_block[8*8];

    // Vertical IDCT-II
    for (uint8 j = 0; j < 8; ++j)
    {
        inverse_transform_8x8_line_fast(src + j, src_pitch, scratch_block + j, 8);
    }

    // Horizontal IDCT-II
    for (uint8 j = 0; j < 8; j++)
    {
        inverse_transform_add_8x8_line_fast(scratch_block + j * 8, 1, add + j * add_pitch, 1, dest + j * dest_pitch, 1 );
    }
}

void sub_transform_8x8(int16 *src, uint32 src_pitch, int16 *sub, uint32 sub_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 scratch_block[8*8];
    int16 sub_scratch_block[8*8];

    // Horizontal IDCT-II
    for (uint8 j = 0; j < 8; j++) 
    {
        sub_8x8_line(src + j * src_pitch, sub + j * sub_pitch, sub_scratch_block + j * 8);
        transform_8x8_line_fast(sub_scratch_block + j * 8, 1, scratch_block + j * 8, 1 );
    }
    
    // Vertical IDCT-II
    for (uint8 j = 0; j < 8; j++)
    {
        transform_8x8_line_fast(scratch_block + j, 8, dest + j, dest_pitch);
    }
}

void transform_16x16_line(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 16; ++i)
    {
        int32 total = 0;             
        int16 *output = dest + i * dest_pitch;
 
        for (uint8 k = 0; k < 16; ++k)
        {
            int16 *input = src + k * src_pitch;
            total += *input * EVX_TRANSFORM_16x16_TRIG_128_LUT[i * 16 + k];
        }

        if (0 == i)
            // mul by sqrt(1 / 16)
            total = (total * 32) / 128;
        else
            // mul by sqrt(2 / 16)
            total = (total * 45) / 128;
 
        total = evx_round_out(total, 64);
        total = total / 128;
        *output = total;
    }
}

void transform_16x16_line_fast(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    evx_post_error(EVX_ERROR_NOTIMPL);
}

void transform_16x16(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 *src_row_8 = src + 8 * src_pitch;
    int16 *dest_row_8 = dest + 8 * dest_pitch;

    transform_8x8(src, src_pitch, dest, dest_pitch);
    transform_8x8(src + 8, src_pitch, dest + 8, dest_pitch);
    transform_8x8(src_row_8, src_pitch, dest_row_8, dest_pitch);
    transform_8x8(src_row_8 + 8, src_pitch, dest_row_8 + 8, dest_pitch);
}

void inverse_transform_16x16_line(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 16; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;
 
        for (uint8 k = 0; k < 16; ++k)
        {
            int16 *input = src + k * src_pitch;
            int32 temp_total = *input * EVX_TRANSFORM_16x16_TRIG_128_LUT[k * 16 + i];

            if (0 == k)
                // mul by sqrt(1 / 16)
                temp_total = (temp_total * 32) / 128;
            else
                // mul by sqrt(2 / 16)
                temp_total = (temp_total * 45) / 128;
                
            total += temp_total;
        }

        total = rounded_div(total, 128);
        *output = total;
    }
}

void inverse_transform_16x16_line_fast(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    evx_post_error(EVX_ERROR_NOTIMPL);
}

void inverse_transform_16x16(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 *src_row_8 = src  + 8 * src_pitch;
    int16 *dest_row_8 = dest + 8 * dest_pitch;

    inverse_transform_8x8(src, src_pitch, dest, dest_pitch);
    inverse_transform_8x8(src + 8, src_pitch, dest + 8, dest_pitch);
    inverse_transform_8x8(src_row_8, src_pitch, dest_row_8, dest_pitch);
    inverse_transform_8x8(src_row_8 + 8, src_pitch, dest_row_8 + 8, dest_pitch);
}

void inverse_transform_add_16x16_line(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    for (uint8 i = 0; i < 16; ++i)
    {
        int32 total = 0;        
        int16 *output = dest + i * dest_pitch;
        int16 *add_input = add + i * add_pitch;
 
        for (uint8 k = 0; k < 16; ++k)
        {
            int16 *input = src + k * src_pitch;
            int32 temp_total = *input * EVX_TRANSFORM_16x16_TRIG_128_LUT[k * 16 + i];

            if (0 == k)
                // mul by sqrt(1 / 16)
                temp_total = (temp_total * 32) / 128;	
            else
                // mul by sqrt(2 / 16)
                temp_total = (temp_total * 45) / 128;	
                
            total += temp_total;
        }

        total = rounded_div(total, 128);
        *output = total + *add_input;
    }
}

void inverse_transform_add_16x16_line_fast(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    evx_post_error(EVX_ERROR_NOTIMPL);
}

void inverse_transform_add_16x16(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 *src_row_8 = src + 8 * src_pitch;
    int16 *add_row_8 = add + 8 * add_pitch;
    int16 *dest_row_8 = dest + 8 * dest_pitch;

    inverse_transform_add_8x8(src, src_pitch, add, add_pitch, dest, dest_pitch);
    inverse_transform_add_8x8(src + 8, src_pitch, add + 8, add_pitch, dest + 8, dest_pitch);
    inverse_transform_add_8x8(src_row_8, src_pitch, add_row_8, add_pitch, dest_row_8, dest_pitch);
    inverse_transform_add_8x8(src_row_8 + 8, src_pitch, add_row_8 + 8, add_pitch, dest_row_8 + 8, dest_pitch);
}

void sub_transform_16x16(int16 *src, uint32 src_pitch, int16 *sub, uint32 sub_pitch, int16 *dest, uint32 dest_pitch)
{
    int16 *src_row_8 = src + 8 * src_pitch;
    int16 *sub_row_8 = sub + 8 * sub_pitch;
    int16 *dest_row_8 = dest + 8 * dest_pitch;

    sub_transform_8x8(src, src_pitch, sub, sub_pitch, dest, dest_pitch);
    sub_transform_8x8(src + 8, src_pitch, sub + 8, sub_pitch, dest + 8, dest_pitch);
    sub_transform_8x8(src_row_8, src_pitch, sub_row_8, sub_pitch, dest_row_8, dest_pitch);
    sub_transform_8x8(src_row_8 + 8, src_pitch, sub_row_8 + 8, sub_pitch, dest_row_8 + 8, dest_pitch);
}

} // namespace evx







