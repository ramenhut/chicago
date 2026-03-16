
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// quantize.h
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

#ifndef __EVX_QUANTIZE_H__
#define __EVX_QUANTIZE_H__

#include "base.h"
#include "config.h"
#include "evx_math.h"
#include "types.h"
#include "macroblock.h"

#define EVX_MAX_MPEG_QUANT_LEVELS             (64)

namespace evx {

// Queries the appropriate adaptive index for a given block. For non-adaptive
// quantization the result will simply be quality.
uint8 query_block_quantization_parameter(uint8 quality, const macroblock &block, EVX_BLOCK_TYPE block_type);

// Performs quantization of source according to the quantization parameter (qp) and the block type.
void quantize_macroblock(uint8 qp, EVX_BLOCK_TYPE block_type, const macroblock &source, macroblock *__restrict dest);

// Performs an inverse quantization of source according to the quantization parameter and block type.
void inverse_quantize_macroblock(uint8 qp, EVX_BLOCK_TYPE block_type, const macroblock &source, macroblock *__restrict dest);

#if EVX_ENABLE_TRELLIS_QUANTIZATION
// Trellis quantization: RD-optimal coefficient level optimization.
// Modifies quantized_block in-place after normal quantization.
void trellis_optimize_macroblock(uint8 qp, EVX_BLOCK_TYPE block_type,
    const macroblock &transform_coeffs, const uint8 *scan_table,
    float lambda, macroblock *quantized_block);
#endif

} // namespace evx

#endif // __EVX_QUANTIZE_H__