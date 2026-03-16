
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// interp.h
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

#ifndef __EVX_INTERP_H__
#define __EVX_INTERP_H__

#include "base.h"
#include "macroblock.h"
#include "imageset.h"

namespace evx {

// Generate a sub-pixel interpolated macroblock using a 6-tap Wiener filter
// [1, -5, 20, 20, -5, 1] / 32 (H.264-standard half-pel filter).
//
// base_x, base_y: integer pixel position of the best match
// dir_x, dir_y: sub-pixel direction (-1, 0, or +1 in each axis)
// is_quarter: false = half-pel, true = quarter-pel (average of full-pel and half-pel)
// output: macroblock to write the interpolated result into
void interpolate_subpixel_6tap(const image_set &ref, int32 base_x, int32 base_y,
                                int16 dir_x, int16 dir_y, bool is_quarter,
                                macroblock *output);

} // namespace evx

#endif // __EVX_INTERP_H__
