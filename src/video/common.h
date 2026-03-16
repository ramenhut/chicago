
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// common.h
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

#ifndef __EVX_COMMON_H__
#define __EVX_COMMON_H__

#include <cmath>
#include "base.h"
#include "config.h"
#include "types.h"
#include "imageset.h"
#include "bitstream.h"
#include "abac.h"
#include "macroblock.h"

// The structures defined here are designed to be lightweight and managed
// by the larger codec objects. For this reason, these structures cannot
// contain virtual functions, ctors, or dtors. All initialization should be
// directly instigated.

namespace evx {

#pragma pack( push )
#pragma pack( 2 )

typedef struct evx_header
{
    uint8 magic[4];        // must be 'EVX3'
    uint16 size;           // size of the header.
    uint8 ref_count;       // should match EVX_REFERENCE_FRAME_COUNT.
    uint16 version;                        
    uint16 frame_width;
    uint16 frame_height;

} evx_header;

evx_status initialize_header(uint32 width, uint32 height, evx_header *header);
evx_status verify_header(const evx_header &header);
evx_status clear_header(evx_header *header);

typedef struct evx_frame
{
    EVX_FRAME_TYPE type;    // current frame type
    uint32 index;           // always equals count - 1
    uint16 quality;         // global frame quality
#if EVX_ENABLE_B_FRAMES
    uint32 display_order;   // presentation order (POC)
    uint8 is_reference;     // 1 if this B-frame is stored in DPB
    uint8 dpb_slot;         // which prediction_cache slot to decode into
    uint8 ref_slot_fwd;     // DPB slot for forward (past) reference
    uint8 ref_slot_bwd;     // DPB slot for backward (future) reference
#endif

} evx_frame;

evx_status clear_frame(evx_frame *frame);

typedef struct evx_block_desc
{
    EVX_BLOCK_TYPE block_type;

    uint8 prediction_target;    // the target prediction frame (0 for intra, 1 for inter, etc.)
                                // note this is omitted for intra frames (since it must be 0).
    int16 motion_x;
    int16 motion_y;
                                
    bool sp_pred;               // True if subpixel prediction is used.
    bool sp_amount;             // 0 = half pixel, 1 = quarter pixel
    uint8 sp_index;             // 3 bits - specifies the direction of the prediction
    uint8 q_index;              // per block quantization index level.
    uint8 intra_mode;           // 0-6 directional, 7 = none (EVX_INTRA_MODE_NONE)

    // peek and debug only:
    int16 variance;             // pre-q block variance.

#if EVX_ENABLE_B_FRAMES
    uint8 prediction_mode;      // 0=forward, 1=backward, 2=bi-predicted
    uint8 prediction_target_b;  // second reference slot (for bi-predicted)
    int16 motion_x_b;
    int16 motion_y_b;
    bool sp_pred_b;
    bool sp_amount_b;
    uint8 sp_index_b;
#endif

} evx_block_desc;

evx_status clear_block_desc(evx_block_desc *desc);

#pragma pack(pop)

// The cache bank is directly managed by the context. It's primary purpose
// is to restrict the pipeline's context access to the images caches.

typedef struct evx_cache_bank
{
    image_set input_cache;            // yuv420p view of the source image.
    image_set output_cache;           // transformed and quantized view.
    image_set transform_cache;        // scratch buffer used for transform ops.
    image_set motion_cache;           // cache for motion interpolated blocks.
    image_set prediction_cache[EVX_REFERENCE_FRAME_COUNT];
    image_set staging_cache;          // used during serialization for ordering.

    macroblock transform_block;       // static cache for transform operations.
    macroblock motion_block;          // static cache for motion interpolation.
    macroblock staging_block;         // static cache for staging.

} evx_cache_bank;

// Metadata entropy contexts for unified per-block serialization.
#if EVX_ENABLE_UNIFIED_METADATA
struct evx_metadata_contexts {
    entropy_context block_type_ctx;
    entropy_context intra_mode_ctx;
    entropy_context pred_target_ctx;
    entropy_context mvd_x_ctx;
    entropy_context mvd_y_ctx;
    entropy_context sp_pred_ctx;
    entropy_context sp_amount_ctx;
    entropy_context sp_index_ctx;
    entropy_context quality_ctx;
#if EVX_ENABLE_MPM
    entropy_context mpm_flag_ctx;
    entropy_context mpm_idx_ctx;
#endif
#if EVX_ENABLE_SAO
    entropy_context sao_merge_ctx;
    entropy_context sao_type_ctx;
    entropy_context sao_eo_class_ctx;
    entropy_context sao_offset_ctx;
    entropy_context sao_band_pos_ctx;
#endif
#if EVX_ENABLE_B_FRAMES
    entropy_context pred_mode_ctx;
    entropy_context pred_target_b_ctx;
    entropy_context mvd_x_b_ctx;
    entropy_context mvd_y_b_ctx;
    entropy_context sp_pred_b_ctx;
    entropy_context sp_amount_b_ctx;
    entropy_context sp_index_b_ctx;
#endif
    void clear() {
        block_type_ctx.clear();
        intra_mode_ctx.clear();
        pred_target_ctx.clear();
        mvd_x_ctx.clear();
        mvd_y_ctx.clear();
        sp_pred_ctx.clear();
        sp_amount_ctx.clear();
        sp_index_ctx.clear();
        quality_ctx.clear();
#if EVX_ENABLE_MPM
        mpm_flag_ctx.clear();
        mpm_idx_ctx.clear();
#endif
#if EVX_ENABLE_SAO
        sao_merge_ctx.clear();
        sao_type_ctx.clear();
        sao_eo_class_ctx.clear();
        sao_offset_ctx.clear();
        sao_band_pos_ctx.clear();
#endif
#if EVX_ENABLE_B_FRAMES
        pred_mode_ctx.clear();
        pred_target_b_ctx.clear();
        mvd_x_b_ctx.clear();
        mvd_y_b_ctx.clear();
        sp_pred_b_ctx.clear();
        sp_amount_b_ctx.clear();
        sp_index_b_ctx.clear();
#endif
    }
};
#endif

#if EVX_ENABLE_SAO
struct evx_sao_info;
#endif

typedef struct evx_context
{
    entropy_coder arith_coder;         // single entropy coder engine for all data
#if EVX_ENABLE_SIGMAP_CODING
    entropy_context coeff_contexts[EVX_SIGMAP_CONTEXTS_PER_SET * 2]; // 176: intra (0-87), inter (88-175)
#define EVX_NUM_COEFF_CONTEXTS (EVX_SIGMAP_CONTEXTS_PER_SET * 2)
#elif EVX_ENABLE_SPLIT_ENTROPY_CONTEXTS
    entropy_context coeff_contexts[8]; // intra DC/low/mid/high (0-3), inter DC/low/mid/high (4-7)
#define EVX_NUM_COEFF_CONTEXTS 8
#else
    entropy_context coeff_contexts[4]; // DC, low-AC, mid-AC, high-AC probability models
#define EVX_NUM_COEFF_CONTEXTS 4
#endif
#if EVX_ENABLE_UNIFIED_METADATA
    evx_metadata_contexts meta_contexts;
#endif
    bit_stream feed_stream;           // useful for feeding the entropy coder.

    evx_block_desc *block_table;
#if EVX_ENABLE_TEMPORAL_MVP
    evx_block_desc *prev_block_table;
#endif
#if EVX_ENABLE_SAO
    evx_sao_info *sao_table;
#endif
    evx_cache_bank cache_bank;        // bank of caches used by the pipeline.

    uint32 width_in_blocks;           // width of our full context space, in blocks
    uint32 height_in_blocks;          // height of our full context space, in blocks

    evx_context();
} evx_context;

// initialize_context will allocate the necessary space for all internal 
// context buffers. This should only be done once for each coding session.
evx_status initialize_context(uint32 width, uint32 height, evx_context *context);

// query_context* return the requested dimension of the context in pixels.
uint32 query_context_width(const evx_context &context);
uint32 query_context_height(const evx_context &context);

// clear_context will deallocate all internal buffers. Since all internal objects
// will deallocate themselves upon destruction, ClearContext is only called
// if the user wishes to reuse an existing coder for a new stream.
evx_status clear_context(evx_context *context);

uint32 query_prediction_index_by_offset(const evx_frame &frame, uint8 offset);

#if EVX_ENABLE_B_FRAMES
uint32 assign_ip_dpb_slot(uint32 ip_index);
uint32 assign_bref_dpb_slot();
uint32 assign_b_scratch_slot();
uint32 assign_b_scratch_slot_for_count(uint32 b_frame_count);
#endif

inline float compute_lambda(uint16 quality) {
    return 0.85f * powf(2.0f, (quality - 12.0f) / 3.0f);
}

#if EVX_ENABLE_MPM
void compute_intra_mpm(const evx_block_desc *block_table, uint32 width_in_blocks,
                        uint32 block_index, uint8 mpm[2]);
#endif

} // namespace evx

#endif // __EVX_COMMON_H__