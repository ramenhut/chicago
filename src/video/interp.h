
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
