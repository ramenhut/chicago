
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// xftables.h
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

#ifndef __EVX_TRANSFORM_TABLES_H__
#define __EVX_TRANSFORM_TABLES_H__

#include "base.h"

namespace evx {

// This table stores the results of the primary trigonometric computation
// of the forward and inverse DCT-2 calculations, scaled by 128:
//
//    EVX_TRANSFORM_TRIG_LUT[j * 8 + i] = cos(((2 * i + 1) * j * EVX_PI) / (2 * length))
//
// Note that the inverse is merely the transpose of this matrix, so we swap i and j for the IDCT.
// We've chosen to scale our trig coefficients by 128 because it provides us adequate precision 
// without overflowing our 32 bit integer range. 

const int16 EVX_TRANSFORM_4x4_TRIG_128_LUT[] = 
{
    128,  128,  128,  128,
    118,   49,  -49, -118,
     91,  -91,  -91,   91,
     49, -118,  118,  -49
};

const int16 EVX_TRANSFORM_8x8_TRIG_128_LUT[] = 
{
    128,  128,  128,  128,  128,  128,  128,  128, 
    126,  106,   71,   25,  -25,  -71, -106, -126, 
    118,   49,  -49, -118, -118,  -49,   49,  118, 
    106,  -25, -126,  -71,   71,  126,   25, -106, 
     91,  -91,  -91,   91,   91,  -91,  -91,   91, 
     71, -126,   25,  106, -106,  -25,  126,  -71, 
     49, -118,  118,  -49,  -49,  118, -118,   49, 
     25,  -71,  106, -126,  126, -106,   71,  -25
};

const int16 EVX_TRANSFORM_16x16_TRIG_128_LUT[] = 
{
    128,  128,  128,  128,  128,  128,  128,  128,  128,  128,  128,  128,  128,  128,  128,  128,
    127,  122,  113,   99,   81,   60,   37,   13,  -13,  -37,  -60,  -81,  -99, -113, -122, -127,
    126,  106,   71,   25,  -25,  -71, -106, -126, -126, -106,  -71,  -25,   25,   71,  106,  126,
    122,   81,   13,  -60, -113, -127,  -99,  -37,   37,   99,  127,  113,   60,  -13,  -81, -122,
    118,   49,  -49, -118, -118,  -49,   49,  118,  118,   49,  -49, -118, -118,  -49,   49,  118,
    113,   13,  -99, -122,  -37,   81,  127,   60,  -60, -127,  -81,   37,  122,   99,  -13, -113,
    106,  -25, -126,  -71,   71,  126,   25, -106, -106,   25,  126,   71,  -71, -126,  -25,  106,
     99,  -60, -122,   13,  127,   37, -113,  -81,   81,  113,  -37, -127,  -13,  122,   60,  -99,
     91,  -91,  -91,   91,   91,  -91,  -91,   91,   91,  -91,  -91,   91,   91,  -91,  -91,   91,
     81, -113,  -37,  127,  -13, -122,   60,   99,  -99,  -60,  122,   13, -127,   37,  113,  -81,
     71, -126,   25,  106, -106,  -25,  126,  -71,  -71,  126,  -25, -106,  106,   25, -126,   71,
     60, -127,   81,   37, -122,   99,   13, -113,  113,  -13,  -99,  122,  -37,  -81,  127,  -60,
     49, -118,  118,  -49,  -49,  118, -118,   49,   49, -118,  118,  -49,  -49,  118, -118,   49,
     37,  -99,  127, -113,   60,   13,  -81,  122, -122,   81,  -13,  -60,  113, -127,   99,  -37,
     25,  -71,  106, -126,  126, -106,   71,  -25,  -25,   71, -106,  126, -126,  106,  -71,   25,
     13,  -37,   60,  -81,   99, -113,  122, -127,  127, -122,  113,  -99,   81,  -60,   37,  -13,
};

} // namespace evx

#endif // __EVX_TRANSFORM_TABLES_H__