
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// config.h
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

#ifndef __EVX_CONFIG_H__
#define __EVX_CONFIG_H__

// Frame parameters

#define EVX_ALLOW_INTER_FRAMES                                      (1)
#define EVX_REFERENCE_FRAME_COUNT                                   (4)
#define EVX_DEFAULT_QUALITY_LEVEL                                   (8)
#define EVX_PERIODIC_INTRA_RATE                                     (3600)     // 0 implies only i-frames
#define EVX_ENABLE_CHROMA_SUPPORT                                   (1)        // 0 - grayscale, 1 - color

// Quantization parameters. Disabling quantization will enable a high
// quality semi-lossless mode.

#define EVX_QUANTIZATION_ENABLED                                    (1)
#define EVX_ENABLE_LINEAR_QUANTIZATION                              (0)        // 0 - MPEG, 1 - H.263
#define EVX_ROUNDED_QUANTIZATION                                    (1)      
#define EVX_ADAPTIVE_QUANTIZATION                                   (1)

// Deblocking parameters
#define EVX_ENABLE_DEBLOCKING                                       (1)

// RDO parameters
#define EVX_ENABLE_RDO                                              (1)

// SATD metric for motion estimation
#define EVX_ENABLE_SATD_METRIC                                      (1)

// Frequency-dependent quantization deadzone for inter blocks
#define EVX_ENABLE_FREQ_DEADZONE                                    (1)

// 6-tap Wiener filter for sub-pixel motion compensation
#define EVX_ENABLE_6TAP_INTERPOLATION                               (1)

// Chroma QP offset: quantize chroma more aggressively than luma
#define EVX_CHROMA_QP_OFFSET                                        (2)

// Fast intra mode decision via gradient analysis (prune to 3 candidates)
#define EVX_ENABLE_FAST_INTRA_MODE                                  (1)

// QP-adaptive deblocking: boost alpha/beta index at strength-2 boundaries
#define EVX_DEBLOCK_QP_OFFSET                                       (2)

// QP-adaptive inter deadzone: widen deadzone at high QP (>= 16)
#define EVX_ENABLE_QP_ADAPTIVE_DEADZONE                             (1)

// Adaptive motion search radius: reduce radius when initial match is good
#define EVX_ENABLE_ADAPTIVE_SEARCH_RADIUS                           (1)

// Full RDO evaluation of all 3 gradient-pruned intra mode candidates
#define EVX_ENABLE_RDO_INTRA_MODES                                  (1)

// Separate entropy contexts for intra (0-3) and inter (4-7) blocks
#define EVX_ENABLE_SPLIT_ENTROPY_CONTEXTS                           (1)

// Frequency-dependent deadzone for intra quantization (lighter than inter)
#define EVX_ENABLE_INTRA_DEADZONE                                   (1)

// RDO evaluation of copy blocks against intra/delta alternatives
#define EVX_ENABLE_RDO_COPY_EVAL                                    (1)

// Per-block unified metadata serialization with field-specific entropy contexts
#define EVX_ENABLE_UNIFIED_METADATA                                 (1)

// Significance-map coefficient coding with per-position contexts
#define EVX_ENABLE_SIGMAP_CODING                                    (1)
#define EVX_SIGMAP_CONTEXTS_PER_SET                                 (88)

// Trellis quantization: RD-optimal coefficient level optimization
#define EVX_ENABLE_TRELLIS_QUANTIZATION                             (1)

// Wavefront Parallel Processing: encode/decode macroblock rows in parallel
#define EVX_ENABLE_WPP                                              (1)

// Most Probable Mode: predict intra mode from neighbors to reduce coding cost
#define EVX_ENABLE_MPM                                              (1)

// Temporal MV predictor: use co-located block from previous frame as MV candidate
#define EVX_ENABLE_TEMPORAL_MVP                                     (1)

// Sample Adaptive Offset: per-macroblock offset correction after deblocking
#define EVX_ENABLE_SAO                                              (1)

// SAO chroma-aware merge: include chroma distortion in merge decisions
#define EVX_ENABLE_SAO_CHROMA_MERGE                                 (0)

// B-frame bidirectional prediction
#define EVX_ENABLE_B_FRAMES                                             (1)
#define EVX_B_FRAME_COUNT                                               (3)    // 0-3 B-frames between P-frames

// Diagnostic: disable bi-predicted mode (prediction_mode==2) to isolate bidir artifacts.
// B-frames still use forward/backward-only inter prediction when this is enabled.
#define EVX_DISABLE_BIDIR_PRED                                          (0)

// Diagnostic: force all B-frame blocks to intra-only (no inter references at all).
// If artifacts disappear, the bug is in B-frame inter prediction.
// If artifacts persist, the bug is in B-frame infrastructure (DPB, display reorder, etc.)
#define EVX_FORCE_INTRA_BFRAMES                                         (0)

// Diagnostic: disable backward-only inter on B-frames (keep forward-only + intra).
#define EVX_DISABLE_BWD_PRED                                            (0)

// Diagnostic: disable forward-only inter on B-frames (keep backward-only + intra).
#define EVX_DISABLE_FWD_PRED                                            (0)

#endif // __EVX_CONFIG_H__
