
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// types.h
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

#ifndef __EVX_TYPES_H__
#define __EVX_TYPES_H__

#include "base.h"
#include "config.h"

// The structures defined here are designed to be lightweight and managed
// by the larger codec objects. For this reason, these structures cannot
// contain virtual functions, ctors, or dtors. All initialization should be
// directly instigated.

namespace evx {

enum EVX_FRAME_TYPE
{
    EVX_FRAME_INTRA              = 0,
    EVX_FRAME_INTER              = 1,
#if EVX_ENABLE_B_FRAMES
    EVX_FRAME_BIDIR              = 2,
#endif
    EVX_FRAME_FORCE_UINT8        = 0x7F
};

enum EVX_CHANNEL_TYPE
{
    EVX_CHANNEL_TYPE_LUMA       = 0,
    EVX_CHANNEL_TYPE_CHROMA     = 1,
    EVX_CHANNEL_FORCE_UINT8     = 0x7F
};

// Block types
//                             source          motion?          operation
//  intra block default        i               n                copy
//  intra motion copy          i               y                copy
//  intra motion delta         i               y                sub
//  inter block delta          p               n                sub
//  inter motion copy          p               y                copy
//  inter motion delta         p               y                sub

#define EVX_MAKE_BLOCK_TYPE_CODE(intra, motion, copy) ((intra) | ((motion) << 0x1) | ((copy) << 0x2))
#define EVX_IS_INTRA_BLOCK_TYPE(type) (!!((type) & 0x1))
#define EVX_IS_MOTION_BLOCK_TYPE(type) (!!((type) & 0x2))
#define EVX_IS_COPY_BLOCK_TYPE(type) (!!((type) & 0x4))

#define EVX_SET_INTRA_BLOCK_TYPE_BIT(type, mode) ((type) = (EVX_BLOCK_TYPE) (((type) & ~0x1) | !!(mode))) 
#define EVX_SET_MOTION_BLOCK_TYPE_BIT(type, mode) ((type) = (EVX_BLOCK_TYPE) (((type) & ~0x2) | (!!(mode) << 1)))
#define EVX_SET_COPY_BLOCK_TYPE_BIT(type, mode) ((type) = (EVX_BLOCK_TYPE) (((type) & ~0x4) | (!!(mode) << 2)))

enum EVX_BLOCK_TYPE
{
    EVX_BLOCK_INTRA_DEFAULT         = EVX_MAKE_BLOCK_TYPE_CODE(true, false, false),
    EVX_BLOCK_INTRA_MOTION_COPY     = EVX_MAKE_BLOCK_TYPE_CODE(true, true, true),
    EVX_BLOCK_INTRA_MOTION_DELTA    = EVX_MAKE_BLOCK_TYPE_CODE(true, true, false),
    EVX_BLOCK_INTER_COPY            = EVX_MAKE_BLOCK_TYPE_CODE(false, false, true),   // in place copy
    EVX_BLOCK_INTER_DELTA           = EVX_MAKE_BLOCK_TYPE_CODE(false, false, false),  // in place delta
    EVX_BLOCK_INTER_MOTION_COPY     = EVX_MAKE_BLOCK_TYPE_CODE(false, true, true),    // out of place copy
    EVX_BLOCK_INTER_MOTION_DELTA    = EVX_MAKE_BLOCK_TYPE_CODE(false, true, false),   // out of place delta
    EVX_BLOCK_FORCE_BYTE            = 0x7F,
};

} // namespace evx

#endif // __EVX_TYPES_H__