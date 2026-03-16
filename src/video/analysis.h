
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// analysis.h
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

#ifndef __EVX_BLOCK_ANALYSIS_H__
#define __EVX_BLOCK_ANALYSIS_H__

#include "base.h"
#include "macroblock.h"

namespace evx {

// Hadamard 4x4 transform: applies butterfly in both dimensions.
// Input: 4x4 difference block (src[row*stride + col]).
// Output: 16 transformed coefficients in out[0..15].
inline void hadamard_4x4(const int16 *src, int32 stride, int32 *out)
{
    int32 tmp[16];

    // Horizontal pass
    for (int i = 0; i < 4; i++)
    {
        int32 a = src[i * stride + 0];
        int32 b = src[i * stride + 1];
        int32 c = src[i * stride + 2];
        int32 d = src[i * stride + 3];

        int32 s0 = a + b;
        int32 s1 = a - b;
        int32 s2 = c + d;
        int32 s3 = c - d;

        tmp[i * 4 + 0] = s0 + s2;
        tmp[i * 4 + 1] = s1 + s3;
        tmp[i * 4 + 2] = s0 - s2;
        tmp[i * 4 + 3] = s1 - s3;
    }

    // Vertical pass
    for (int j = 0; j < 4; j++)
    {
        int32 a = tmp[0 * 4 + j];
        int32 b = tmp[1 * 4 + j];
        int32 c = tmp[2 * 4 + j];
        int32 d = tmp[3 * 4 + j];

        int32 s0 = a + b;
        int32 s1 = a - b;
        int32 s2 = c + d;
        int32 s3 = c - d;

        out[0 * 4 + j] = s0 + s2;
        out[1 * 4 + j] = s1 + s3;
        out[2 * 4 + j] = s0 - s2;
        out[3 * 4 + j] = s1 - s3;
    }
}

// Computes the Sum of Absolute Transformed Differences (SATD) for a 16x16 block.
// Partitions into 4x4 sub-blocks, applies Hadamard transform, sums absolute values.
inline int32 compute_block_satd(const macroblock &left, const macroblock &right)
{
    int32 satd = 0;
    int16 diff[16];
    int32 coeff[16];

    for (int by = 0; by < EVX_MACROBLOCK_SIZE; by += 4)
    for (int bx = 0; bx < EVX_MACROBLOCK_SIZE; bx += 4)
    {
        // Compute 4x4 difference block
        for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
        {
            diff[r * 4 + c] = left.data_y[(by + r) * left.stride + (bx + c)]
                             - right.data_y[(by + r) * right.stride + (bx + c)];
        }

        hadamard_4x4(diff, 4, coeff);

        for (int k = 0; k < 16; k++)
        {
            satd += abs(coeff[k]);
        }
    }

    // Normalize: the Hadamard doubles energy in each dimension, so divide by 2.
    return (satd + 1) >> 1;
}

// Computes a sum of absolute differences between two blocks.
inline int32 compute_block_sad(const macroblock &left, const macroblock &right)
{
    int32 sad = 0;
    int32 temp = 0;

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        temp = (left.data_y[j * left.stride + i] - right.data_y[j * right.stride + i]);
        sad += abs(temp);
    }  

    return sad;
}

inline int32 compute_block_sad(const macroblock &delta)
{
    int32 sad  = 0;

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        sad += abs(delta.data_y[ j * delta.stride + i]);                      
    }  

    return sad;
}

// Computes the mean squared error of two blocks.
inline int32 compute_block_mse(const macroblock &left, const macroblock &right)
{
    int32 mse = 0;
    int32 temp = 0;

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        temp = (left.data_y[j *left.stride + i] - right.data_y[j * right.stride + i]);
        mse += (temp * temp);                    
    }  

    return mse >> (EVX_MACROBLOCK_SHIFT + EVX_MACROBLOCK_SHIFT);
}

// Computes a sum of squared differences between two blocks.
inline int32 compute_block_ssd(const macroblock &left, const macroblock &right)
{
    int32 ssd = 0;
    int32 temp = 0;

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        temp = (left.data_y[j * left.stride + i] - right.data_y[ j * right.stride + i ]);
        ssd += temp * temp;                      
    }  

    return ssd;
}

// Computes the maximum absolute difference between two blocks.
inline int32 compute_block_mad(const macroblock &left, const macroblock &right)
{
    int32 mad = 0;

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        int32 temp = abs(left.data_y[j * left.stride + i] - right.data_y[j * right.stride + i]);
        mad = evx_max2(temp, mad);                     
    }  

    // We examine the chroma channels to avoid skipping a potentially significant block.
    for (uint32 j = 0; j < (EVX_MACROBLOCK_SIZE >> 1); ++j)                                                
    for (uint32 i = 0; i < (EVX_MACROBLOCK_SIZE >> 1); ++i)                                                
    {          
        int32 temp_u = abs(left.data_u[j * (left.stride >> 1) + i] - right.data_u[j * (right.stride >> 1) + i]);
        int32 temp_v = abs(left.data_v[j * (left.stride >> 1) + i] - right.data_v[j * (right.stride >> 1) + i]);
        mad = evx_max2(temp_u, mad);    
        mad = evx_max2(temp_v, mad); 
    }  

    return mad;
}

// Computes the mean of the block.
inline int32 compute_block_mean(const macroblock &src)
{
    int32 mean = 0;

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        mean += src.data_y[j * src.stride + i];                   
    }  

    return (mean + 128) >> 8;
}

inline int16 compute_nonzero_block_mean(const macroblock &src)
{
    int32 mean = 0;
    int16 count = 0;

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {         
        if (src.data_y[j * src.stride + i] != 0)
        {
            mean += abs(src.data_y[j * src.stride + i]);  
            count++;
        }
    }  
    
    return (count ? rounded_div(mean, count) : 0);
}

// Computes the variance of the block.
inline int32 compute_block_variance(const macroblock &src)
{
    int32 temp = 0;
    int32 variance = 0;
    int32 mean = compute_block_mean(src);

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        temp = src.data_y[ j * src.stride + i ] - mean;
        variance += abs(temp); // * temp;                   
    }  

    return (variance + 128) >> 8;
}

inline int32 compute_block_variance2(const macroblock &src)
{
    int32 sum = 0;
    int32 temp = 0;
    int32 count = 0;
    int32 sum_of_squares = 0;

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {          
        if (i == 0 && j == 0) continue;

        if (src.data_y[j * src.stride + i])
        {
            temp = src.data_y[j * src.stride + i];
            sum += temp;
            sum_of_squares += temp * temp;   
            count++;
        }
    }  

    return (count > 0 ? sum_of_squares - rounded_div(sum * sum, count) : 0); // + 128) >> 8);
}

inline int16 compute_block_variance3(const macroblock &src)
{
    int16 mean = 0;
    int16 count = 0;
    int32 variance = 0;

    variance = compute_nonzero_block_mean(src);

    for (uint32 j = 0; j < EVX_MACROBLOCK_SIZE; ++j)                                                
    for (uint32 i = 0; i < EVX_MACROBLOCK_SIZE; ++i)                                                
    {    
        if (i == 0 && j == 0) continue;

        if (src.data_y[ j * src.stride + i ] != 0)
        {
            variance += abs(src.data_y[ j * src.stride + i ] - mean);
            count++;
        }
    }

    return (count ? rounded_div(variance, count) : 0);
}

} // namespace evx

#endif // __EVX_BLOCK_ANALYSIS_H__