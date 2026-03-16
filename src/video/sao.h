
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// sao.h
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

#ifndef __EVX_SAO_H__
#define __EVX_SAO_H__

#include "base.h"
#include "config.h"
#include "imageset.h"

#if EVX_ENABLE_SAO && !EVX_ENABLE_UNIFIED_METADATA
#error "EVX_ENABLE_SAO requires EVX_ENABLE_UNIFIED_METADATA for entropy-coded serialization"
#endif

namespace evx {

#define EVX_SAO_BAND_COUNT      (32)
#define EVX_SAO_BAND_WIDTH      (8)
#define EVX_SAO_MAX_OFFSET      (7)
#define EVX_SAO_NUM_OFFSETS     (4)

#define EVX_SAO_OFF             (0)
#define EVX_SAO_EDGE            (1)
#define EVX_SAO_BAND            (2)

#define EVX_SAO_EO_HORIZ        (0)
#define EVX_SAO_EO_VERT         (1)
#define EVX_SAO_EO_DIAG_135     (2)
#define EVX_SAO_EO_DIAG_45      (3)

#define EVX_SAO_EO_CAT_VALLEY   (0)
#define EVX_SAO_EO_CAT_CONCAVE  (1)
#define EVX_SAO_EO_CAT_CONVEX   (2)
#define EVX_SAO_EO_CAT_PEAK     (3)
#define EVX_SAO_EO_CAT_NONE     (4)

struct evx_sao_params {
    uint8 type;
    uint8 eo_class;
    uint8 band_position;
    int8 offsets[EVX_SAO_NUM_OFFSETS];
};

struct evx_sao_info {
    bool merge_left;
    bool merge_above;
    evx_sao_params luma;
    evx_sao_params chroma;
};

inline void clear_sao_params(evx_sao_params *p) {
    p->type = EVX_SAO_OFF;
    p->eo_class = 0;
    p->band_position = 0;
    p->offsets[0] = p->offsets[1] = p->offsets[2] = p->offsets[3] = 0;
}

inline void clear_sao_info(evx_sao_info *info) {
    info->merge_left = false;
    info->merge_above = false;
    clear_sao_params(&info->luma);
    clear_sao_params(&info->chroma);
}

void sao_filter_plane(const evx_sao_info *sao_table, uint32 width_in_blocks, uint32 height_in_blocks,
                      image *plane, uint16 mb_size, bool is_luma);

evx_status sao_filter_image(const evx_sao_info *sao_table, uint32 width_in_blocks, uint32 height_in_blocks,
                            image_set *target);

evx_status sao_encode_frame(const image_set &source, image_set *reconstructed,
                            evx_sao_info *sao_table, uint32 width_in_blocks, uint32 height_in_blocks,
                            float lambda);

} // namespace evx

#endif // __EVX_SAO_H__
