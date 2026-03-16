
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// macroblock.h
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Additional Information:
//
//   For more information, visit http://www.bertolami.com.
*/

#ifndef __EVX_MACROBLOCK_H__
#define __EVX_MACROBLOCK_H__

#include "base.h"
#include "abac.h"
#include "config.h"
#include "golomb.h"
#include "image.h"
#include "imageset.h"
#include "evx_memory.h"
#include "stream.h"
#include "transform.h"

// Block types
//                            source          motion?          operation
//  intra block default        i               n                copy
//  intra motion skip          i               y                copy
//  intra motion delta         i               y                sub
//  inter block delta          p               n                sub
//  inter motion skip          p               y                copy
//  inter motion delta         p               y                sub

#define EVX_MACROBLOCK_SIZE                         (16)
#define EVX_MACROBLOCK_SHIFT                        (log2((uint8) EVX_MACROBLOCK_SIZE))
#define EVX_MACROBLOCK_LUMINANCE_SIZE               (EVX_MACROBLOCK_SIZE * EVX_MACROBLOCK_SIZE)
#define EVX_MACROBLOCK_CHROMINANCE_SIZE             (EVX_MACROBLOCK_LUMINANCE_SIZE >> 2)
#define EVX_MACROBLOCK_SS_CHROMINANCE_SIZE          (EVX_MACROBLOCK_CHROMINANCE_SIZE >> 2)
#define EVX_MACROBLOCK_FULL_BLOCK_SIZE              (EVX_MACROBLOCK_LUMINANCE_SIZE + EVX_MACROBLOCK_CHROMINANCE_SIZE << 1)

namespace evx {

// A macroblock holds one complete block worth of image data in YUV format.
// All blocks alias an existing memory buffer, and destruction of a block does 
// not imply deinitialization of the source data.

typedef struct macroblock
{
    int16 *data_y;        
    int16 *data_u;
    int16 *data_v;
    uint32 stride;      // stride of the luma channel in elements, not bytes.

} macroblock;
 
// CreateBlock
//
//   Creates a new macroblock pointing to (pixel_x, pixel_y) within the source
//   image. Macroblocks use weak references and do no parameter checking.

inline void create_macroblock(const image_set &src, uint32 pixel_x, uint32 pixel_y, macroblock *dest)
{
    dest->data_y = reinterpret_cast<int16 *>(src.query_y_image()->query_data() + src.query_y_image()->query_block_offset(pixel_x, pixel_y));
    dest->data_u = reinterpret_cast<int16 *>(src.query_u_image()->query_data() + src.query_u_image()->query_block_offset(pixel_x >> 1, pixel_y >> 1));
    dest->data_v = reinterpret_cast<int16 *>(src.query_v_image()->query_data() + src.query_v_image()->query_block_offset(pixel_x >> 1, pixel_y >> 1));
    dest->stride = src.query_y_image()->query_row_pitch() >> 1;   /* elements, not bytes */
}

inline void clear_macroblock(macroblock *block)                                   
{                                                                                   
    for (int32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {                                                                               
        aligned_zero_memory(block->data_y + i * block->stride, sizeof(int16) * EVX_MACROBLOCK_SIZE);    
    }  

    for ( uint32 i = 0; i < ( EVX_MACROBLOCK_SIZE >> 1 ); i++ )                                                
    {                                                                               
        aligned_zero_memory(block->data_u + i * (block->stride >> 1), sizeof(int16) * (EVX_MACROBLOCK_SIZE >> 1));    
        aligned_zero_memory(block->data_v + i * (block->stride >> 1), sizeof(int16) * (EVX_MACROBLOCK_SIZE >> 1));    
    } 
}

inline void print_macroblock(const macroblock &block)                                   
{       
    evx_msg("Printing block:");
    evx_msg("data_y = ");
    evx_msg("");

    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i) 
    {
        printf("    ");

        for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)
        {                                            
            int16 value = block.data_y[i * block.stride + j];
            printf("%i ", value);   
        }  

        printf("\n");
    }

    evx_msg("data_u = ");
    evx_msg("");

    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE >> 1; ++i) 
    {
        printf("    ");

        for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE >> 1; ++j)
        {                                            
            int16 value = block.data_u[i * (block.stride >> 1) + j];
            printf("%i ", value);   
        }  

        printf("\n");
    }

    evx_msg("data_v = ");
    evx_msg("");

    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE >> 1; ++i) 
    {
        printf("    ");

        for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE >> 1; ++j)
        {                                            
            int16 value = block.data_v[i * (block.stride >> 1) + j];

            printf("%i ", value);   
        }  

        printf("\n");
    }
} 

inline void copy_macroblock(const macroblock &src, macroblock *dest)
{                                           
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)
    {                                                                               
        aligned_byte_copy((src.data_y + i * src.stride), sizeof(int16) * EVX_MACROBLOCK_SIZE, (dest->data_y+ i * dest->stride));    
    }  

    for (uint32 i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); i++ )
    {                                                                               
        aligned_byte_copy((src.data_u + i * (src.stride >> 1)), sizeof(int16) * (EVX_MACROBLOCK_SIZE >> 1), (dest->data_u + i * (dest->stride>> 1)));   
        aligned_byte_copy((src.data_v + i * (src.stride >> 1)), sizeof(int16) * (EVX_MACROBLOCK_SIZE >> 1), (dest->data_v + i * (dest->stride >> 1)));    
    }                                                                              
}          

inline void add_macroblock(const macroblock &left, const macroblock &right, macroblock *dest)
{                                                                                   
    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {                                                                               
        dest->data_y[j * dest->stride + i] = left.data_y[j * left.stride + i] + right.data_y[j * right.stride + i]; 
    } 

    for (uint32 j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); ++j)                                                
    for (uint32 i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); ++i)                                                
    {                                                                               
        dest->data_u[j * (dest->stride >> 1) + i] = left.data_u[j * (left.stride >> 1) + i] + right.data_u[j * (right.stride >> 1) + i];     
        dest->data_v[ j * (dest->stride >> 1) + i ] = left.data_v[ j * (left.stride >> 1) + i ] + right.data_v[ j * (right.stride >> 1) + i]; 
    } 
}      

inline void sub_macroblock(const macroblock &left, const macroblock &right, macroblock *dest)
{                                                                                   
    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {                                                                               
        dest->data_y[j * dest->stride + i] = left.data_y[j * left.stride + i] - right.data_y[j * right.stride + i]; 
    } 

    for (uint32 j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); ++j)                                                
    for (uint32 i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); ++i)                                                
    {                                                                               
        dest->data_u[j * (dest->stride >> 1) + i] = left.data_u[j * (left.stride >> 1) + i] - right.data_u[j * (right.stride >> 1) + i];     
        dest->data_v[ j * (dest->stride >> 1) + i ] = left.data_v[ j * (left.stride >> 1) + i ] - right.data_v[ j * (right.stride >> 1) + i]; 
    } 
} 

inline void lerp_macroblock_half(const macroblock &src_a, const macroblock &src_b, macroblock *dest)
{
    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        int32 temp = src_a.data_y[j * src_a.stride + i] + src_b.data_y[j * src_b.stride + i];  
        dest->data_y[j * dest->stride + i] = evx_round_out(temp, 1) / 2;
    }  

    for (uint32 j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); j++)                                                
    for (uint32 i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); i++)                                                
    {          
        int32 temp_u = src_a.data_u[j * (src_a.stride >> 1) + i] + src_b.data_u[j * (src_b.stride >> 1) + i];
        int32 temp_v = src_a.data_v[j * (src_a.stride >> 1) + i] + src_b.data_v[j * (src_b.stride >> 1) + i];

        dest->data_u[j * (dest->stride >> 1) + i] = evx_round_out(temp_u, 1) / 2;
        dest->data_v[j * (dest->stride >> 1) + i] = evx_round_out(temp_v, 1) / 2;
    }
}

inline void lerp_macroblock_quarter(const macroblock &src_a, const macroblock &src_b, macroblock *dest)
{
    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        int32 temp = 3 * src_a.data_y[j * src_a.stride + i] + src_b.data_y[j * src_b.stride + i];
        dest->data_y[j * dest->stride + i] = evx_round_out(temp, 2) / 4;
    }

    for (uint32 j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); j++)                                                
    for (uint32 i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); i++)                                                
    {          
        int32 temp_u = 3 * src_a.data_u[j * (src_a.stride >> 1) + i] + src_b.data_u[j * (src_b.stride >> 1) + i];
        int32 temp_v = 3 * src_a.data_v[j * (src_a.stride >> 1) + i] + src_b.data_v[j * (src_b.stride >> 1) + i];

        dest->data_u[j * (dest->stride >> 1) + i] = evx_round_out(temp_u, 2) / 4;
        dest->data_v[j * (dest->stride >> 1) + i] = evx_round_out(temp_v, 2) / 4;
    }
}

inline void create_subpixel_macroblock(image_set *prediction, bool amount, const macroblock &source, int16 target_x, int16 target_y, macroblock *output)
{ 
    macroblock sp_block;

    create_macroblock(*prediction, target_x, target_y, &sp_block);

    // Create a blerp block using the beta_block and sp_block, 
    // and store results back into beta_block.
    if (!amount)
    {
        lerp_macroblock_half(source, sp_block, output);
    }
    else
    {
        lerp_macroblock_quarter(source, sp_block, output);
    }
}

// Forward block transformations.
//
//   Performs a DCT-II transform on the source block.

inline void transform_macroblock(const macroblock &src, macroblock *dest)
{                                                                                   
    transform_16x16(src.data_y, src.stride, dest->data_y, dest->stride);
    transform_8x8(src.data_u, src.stride >> 1, dest->data_u, dest->stride >> 1);
    transform_8x8(src.data_v, src.stride >> 1, dest->data_v, dest->stride >> 1);
}

inline void sub_transform_macroblock(const macroblock &src, const macroblock &sub, macroblock *dest)                     
{       
    sub_transform_16x16(src.data_y, src.stride, sub.data_y, sub.stride, dest->data_y, dest->stride);
    sub_transform_8x8(src.data_u, src.stride >> 1, sub.data_u, sub.stride >> 1, dest->data_u, dest->stride >> 1);
    sub_transform_8x8(src.data_v, src.stride >> 1, sub.data_v, sub.stride >> 1, dest->data_v, dest->stride >> 1);
}

// Inverse block transformations.
//
//   Performs an Inverse DCT-II transform on the source block.

inline void inverse_transform_macroblock(const macroblock &src, macroblock *dest)
{                                                                                   
    inverse_transform_16x16(src.data_y, src.stride, dest->data_y, dest->stride);
    inverse_transform_8x8(src.data_u, src.stride >> 1, dest->data_u, dest->stride >> 1);
    inverse_transform_8x8(src.data_v, src.stride >> 1, dest->data_v, dest->stride >> 1);
}

inline void inverse_transform_add_macroblock(const macroblock &src, const macroblock &add, macroblock *dest)                     
{       
    inverse_transform_add_16x16(src.data_y, src.stride, add.data_y, add.stride, dest->data_y, dest->stride);
    inverse_transform_add_8x8(src.data_u, src.stride >> 1, add.data_u, add.stride >> 1, dest->data_u, dest->stride >> 1);
    inverse_transform_add_8x8(src.data_v, src.stride >> 1, add.data_v, add.stride >> 1, dest->data_v, dest->stride >> 1);                                      
}

} // namespace evx

#endif // __EVX_MACROBLOCK_H__