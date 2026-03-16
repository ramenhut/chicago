
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// motion.h
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

#ifndef __EVX_MOTION_H__
#define __EVX_MOTION_H__

#include "base.h"
#include "common.h"
#include "macroblock.h"

namespace evx {

void compute_motion_direction_from_frac_index(int16 frac_index, int16 *dir_x, int16 *dir_y);

void compute_mv_predictor(const evx_block_desc *block_table, uint32 width_in_blocks,
                           uint32 block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                           const evx_block_desc *prev_block_table,
#endif
                           int16 *pred_x, int16 *pred_y);

int32 calculate_intra_prediction(const evx_frame &frame, const macroblock &src_block, int32 pixel_x, int32 pixel_y,
                                 evx_cache_bank *cache_bank, evx_block_desc *output_desc);

int32 calculate_inter_prediction(const evx_frame &frame, const macroblock &src_block, int32 pixel_x, int32 pixel_y,
                                 evx_cache_bank *cache_bank, uint16 pred_offset,
                                 const evx_block_desc *block_table, uint32 width_in_blocks, uint32 block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                                 const evx_block_desc *prev_block_table,
#endif
                                 evx_block_desc *output_desc);

#if EVX_ENABLE_B_FRAMES
int32 perform_inter_search(const evx_frame &frame, const macroblock &src_block, int32 pixel_x, int32 pixel_y,
                            evx_cache_bank *cache_bank, uint32 cache_index,
                            const evx_block_desc *block_table, uint32 width_in_blocks, uint32 block_index,
#if EVX_ENABLE_TEMPORAL_MVP
                            const evx_block_desc *prev_block_table,
#endif
                            evx_block_desc *output_desc);

int32 calculate_bidir_prediction(const evx_frame &frame, const macroblock &src_block,
                                  int32 pixel_x, int32 pixel_y,
                                  evx_cache_bank *cache_bank,
                                  uint8 fwd_slot, uint8 bwd_slot,
                                  const evx_block_desc *block_table,
                                  uint32 width_in_blocks, uint32 block_index,
                                  evx_block_desc *output_desc);
#endif

} // namespace evx

#endif // __EVX_MOTION_H__