
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// stream.h
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

#ifndef __EVX_STREAM_H__
#define __EVX_STREAM_H__

#include "base.h"
#include "evx_math.h"
#include "bitstream.h"
#include "abac.h"

namespace evx {

// These methods provide a limited range huffman precoder.
evx_status stream_encode_huffman_value(uint8 value, bit_stream *output);
evx_status stream_decode_huffman_value(bit_stream *data, uint8 *output);

evx_status stream_encode_huffman_values(uint8 *data, uint32 count, bit_stream *output);
evx_status stream_decode_huffman_values(bit_stream *data, uint32 count, uint8 *output);

// These methods provide a stream interface for our golomb precoder.
evx_status stream_encode_value(uint16 value, bit_stream *out_buffer);
evx_status stream_encode_value(int16 value, bit_stream *out_buffer);

evx_status stream_encode_values(uint16 *data, uint32 count, bit_stream *out_buffer);
evx_status stream_encode_values(int16 *data, uint32 count, bit_stream *out_buffer);

evx_status stream_decode_value(bit_stream *data, uint16 *output);
evx_status stream_decode_value(bit_stream *data, int16 *output);

evx_status stream_decode_values(bit_stream *data, uint32 count, uint16 *output);
evx_status stream_decode_values(bit_stream *data, uint32 count, int16 *output);

// These methods combine a golomb precoder with the entropy coder.
evx_status entropy_stream_encode_value(uint16 value, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output);
evx_status entropy_stream_encode_value(int16 value, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output);

evx_status entropy_stream_decode_value(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, uint16 *output);
evx_status entropy_stream_decode_value(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output);

// Block based entropy encoding. we only support signed golomb codes because our 
// dct coefficients are signed 16-bit values.
// 
// Note: raw data buffers must not be padded (stride must equal width).
evx_status entropy_stream_encode_4x4(int16 *input, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output);
evx_status entropy_stream_encode_8x8(int16 *input, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output);
evx_status entropy_stream_encode_16x16(int16 *input, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output);

evx_status entropy_stream_decode_4x4(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output);
evx_status entropy_stream_decode_8x8(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output);
evx_status entropy_stream_decode_16x16(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output);

// run length stream interfaces prefix the output with the number of non-zero coefficients in the block.
evx_status entropy_rle_stream_encode_8x8(int16 *input, bit_stream *feed_stream, entropy_coder *coder, bit_stream *output);
evx_status entropy_rle_stream_decode_8x8(bit_stream *input, entropy_coder *coder, bit_stream *feed_stream, int16 *output);

// Context-switched entropy encode/decode: uses a single coder engine with external context.
evx_status entropy_stream_encode_value_ctx(uint16 value, bit_stream *feed_stream, entropy_coder *coder, entropy_context *ctx, bit_stream *output);
evx_status entropy_stream_encode_value_ctx(int16 value, bit_stream *feed_stream, entropy_coder *coder, entropy_context *ctx, bit_stream *output);
evx_status entropy_stream_decode_value_ctx(bit_stream *input, entropy_coder *coder, entropy_context *ctx, bit_stream *feed_stream, uint16 *output);
evx_status entropy_stream_decode_value_ctx(bit_stream *input, entropy_coder *coder, entropy_context *ctx, bit_stream *feed_stream, int16 *output);

// Context-switched coefficient coding: single coder engine, contexts selected by zigzag position.
// contexts[0]=DC, contexts[1]=low-AC(1-10), contexts[2]=mid-AC(11-40), contexts[3]=high-AC(41-63)
evx_status entropy_rle_stream_encode_8x8_ctx(int16 *input, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output);
evx_status entropy_rle_stream_decode_8x8_ctx(bit_stream *input, entropy_coder *coder, entropy_context *contexts, bit_stream *feed_stream, int16 *output);

// Scan-table-aware variants: use a custom scan order instead of the default zigzag.
evx_status entropy_rle_stream_encode_8x8_ctx(int16 *input, const uint8 *scan_table, bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output);
evx_status entropy_rle_stream_decode_8x8_ctx(bit_stream *input, const uint8 *scan_table, entropy_coder *coder, entropy_context *contexts, bit_stream *feed_stream, int16 *output);

// Significance-map coefficient coding: per-position contexts for sig flags, level coding.
// Context layout per set of EVX_SIGMAP_CONTEXTS_PER_SET (88):
//   0-63:  significance flag per scan position
//   64-67: last_sig_pos (one per position group)
//   68-75: gt1 flag (4 groups x 2 states)
//   76-83: gt2 flag (4 groups x 2 states)
//   84-87: remainder (Golomb fallback per group)
#define EVX_SIGMAP_CTX_SIG_BASE       (0)
#define EVX_SIGMAP_CTX_LAST_BASE      (64)
#define EVX_SIGMAP_CTX_GT1_BASE       (68)
#define EVX_SIGMAP_CTX_GT2_BASE       (76)
#define EVX_SIGMAP_CTX_REMAINDER_BASE (84)

evx_status entropy_sigmap_encode_8x8_ctx(int16 *input, const uint8 *scan_table,
    bit_stream *feed_stream, entropy_coder *coder, entropy_context *contexts, bit_stream *output);
evx_status entropy_sigmap_decode_8x8_ctx(bit_stream *input, const uint8 *scan_table,
    entropy_coder *coder, entropy_context *contexts, bit_stream *feed_stream, int16 *output);

} // namespace evx

#endif // __EVX_STREAM_H__