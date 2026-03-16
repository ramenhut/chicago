
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// transform.h
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

#ifndef __EVX_TRANSFORM_H__
#define __EVX_TRANSFORM_H__

#include "base.h"
#include "evx_math.h"

namespace evx {

// These interfaces perform cosine based transforms on blocks of data.
// Our forward transform is a DCT-II and our inverse transform is simply 
// a DCT-III scaled by 2/N.

// 4x4 transforms
void transform_4x4(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch);
void inverse_transform_4x4(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch);

void sub_transform_4x4(int16 *src, uint32 src_pitch, int16 *sub, uint32 sub_pitch, int16 *dest, uint32 dest_pitch);
void inverse_transform_add_4x4(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch);

// 8x8 transforms
void transform_8x8(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch);
void inverse_transform_8x8(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch);

void sub_transform_8x8(int16 *src, uint32 src_pitch, int16 *sub, uint32 sub_pitch, int16 *dest, uint32 dest_pitch);
void inverse_transform_add_8x8(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch);

// 16x16 transforms
void transform_16x16(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch);
void inverse_transform_16x16(int16 *src, uint32 src_pitch, int16 *dest, uint32 dest_pitch);

void sub_transform_16x16(int16 *src, uint32 src_pitch, int16 *sub, uint32 sub_pitch, int16 *dest, uint32 dest_pitch);
void inverse_transform_add_16x16(int16 *src, uint32 src_pitch, int16 *add, uint32 add_pitch, int16 *dest, uint32 dest_pitch);

} // namespace evx

#endif // __EVX_TRANSFORM_H__

