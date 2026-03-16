
#include "interp.h"
#include "evx_math.h"

namespace evx {

// Sample a pixel with boundary clamping.
static inline int16 sample_clamped(const int16 *data, int32 stride,
                                    int32 x, int32 y, int32 width, int32 height)
{
    x = evx_max2(0, evx_min2(x, width - 1));
    y = evx_max2(0, evx_min2(y, height - 1));
    return data[y * stride + x];
}

// Apply 6-tap filter [1, -5, 20, 20, -5, 1] horizontally at position (x + 0.5, y).
// The half-pel position is between pixel x and x+1.
static inline int16 filter_6tap_h(const int16 *data, int32 stride,
                                   int32 x, int32 y, int32 width, int32 height)
{
    int32 sum = 1  * sample_clamped(data, stride, x - 2, y, width, height)
              - 5  * sample_clamped(data, stride, x - 1, y, width, height)
              + 20 * sample_clamped(data, stride, x,     y, width, height)
              + 20 * sample_clamped(data, stride, x + 1, y, width, height)
              - 5  * sample_clamped(data, stride, x + 2, y, width, height)
              + 1  * sample_clamped(data, stride, x + 3, y, width, height);
    return (int16)((sum + 16) >> 5);
}

// Apply 6-tap filter vertically at position (x, y + 0.5).
static inline int16 filter_6tap_v(const int16 *data, int32 stride,
                                   int32 x, int32 y, int32 width, int32 height)
{
    int32 sum = 1  * sample_clamped(data, stride, x, y - 2, width, height)
              - 5  * sample_clamped(data, stride, x, y - 1, width, height)
              + 20 * sample_clamped(data, stride, x, y,     width, height)
              + 20 * sample_clamped(data, stride, x, y + 1, width, height)
              - 5  * sample_clamped(data, stride, x, y + 2, width, height)
              + 1  * sample_clamped(data, stride, x, y + 3, width, height);
    return (int16)((sum + 16) >> 5);
}

// Interpolate a single plane using the 6-tap filter.
// plane_data: pointer to the R16S plane data
// plane_stride: stride in elements (not bytes)
// plane_width, plane_height: dimensions of the plane
// base_x, base_y: integer position of the block in this plane
// block_size: 16 for luma, 8 for chroma
// dir_x, dir_y: sub-pixel direction
// is_quarter: if true, average full-pel and half-pel
// out_data, out_stride: output macroblock plane
static void interpolate_plane_6tap(const int16 *plane_data, int32 plane_stride,
                                    int32 plane_width, int32 plane_height,
                                    int32 base_x, int32 base_y, int32 block_size,
                                    int16 dir_x, int16 dir_y, bool is_quarter,
                                    int16 *out_data, int32 out_stride)
{
    bool h_shift = (dir_x != 0);
    bool v_shift = (dir_y != 0);

    if (h_shift && !v_shift)
    {
        // Pure horizontal half-pel: filter position is (base_x + offset, base_y + r)
        // When dir_x = -1, half-pel is at (base_x - 0.5), so filter center at x = base_x - 1.
        // When dir_x = +1, half-pel is at (base_x + 0.5), so filter center at x = base_x.
        int32 x_offset = (dir_x > 0) ? 0 : -1;

        for (int32 r = 0; r < block_size; r++)
        for (int32 c = 0; c < block_size; c++)
        {
            int16 half = filter_6tap_h(plane_data, plane_stride,
                                        base_x + c + x_offset, base_y + r,
                                        plane_width, plane_height);
            if (is_quarter)
            {
                int16 full = sample_clamped(plane_data, plane_stride,
                                             base_x + c, base_y + r,
                                             plane_width, plane_height);
                out_data[r * out_stride + c] = (int16)((full + half + 1) >> 1);
            }
            else
            {
                out_data[r * out_stride + c] = half;
            }
        }
    }
    else if (!h_shift && v_shift)
    {
        // Pure vertical half-pel
        int32 y_offset = (dir_y > 0) ? 0 : -1;

        for (int32 r = 0; r < block_size; r++)
        for (int32 c = 0; c < block_size; c++)
        {
            int16 half = filter_6tap_v(plane_data, plane_stride,
                                        base_x + c, base_y + r + y_offset,
                                        plane_width, plane_height);
            if (is_quarter)
            {
                int16 full = sample_clamped(plane_data, plane_stride,
                                             base_x + c, base_y + r,
                                             plane_width, plane_height);
                out_data[r * out_stride + c] = (int16)((full + half + 1) >> 1);
            }
            else
            {
                out_data[r * out_stride + c] = half;
            }
        }
    }
    else if (h_shift && v_shift)
    {
        // Diagonal: apply horizontal 6-tap first, then vertical 6-tap.
        // Need temporary buffer with extra rows for vertical filter overshoot.
        int32 x_offset = (dir_x > 0) ? 0 : -1;
        int32 y_offset = (dir_y > 0) ? 0 : -1;

        // Temporary buffer: block_size columns x (block_size + 5) rows.
        // The vertical filter needs 3 rows above and 2 rows below.
        static const int32 VPAD = 5;
        int16 tmp[(EVX_MACROBLOCK_SIZE) * (EVX_MACROBLOCK_SIZE + VPAD)];
        int32 tmp_stride = block_size;

        // Horizontal pass: produce rows [y_offset - 2 .. y_offset + block_size + 2]
        for (int32 r = -2; r < block_size + 3; r++)
        for (int32 c = 0; c < block_size; c++)
        {
            tmp[(r + 2) * tmp_stride + c] = filter_6tap_h(plane_data, plane_stride,
                base_x + c + x_offset, base_y + r + y_offset,
                plane_width, plane_height);
        }

        // Vertical pass on the temporary buffer.
        for (int32 r = 0; r < block_size; r++)
        for (int32 c = 0; c < block_size; c++)
        {
            // The temp buffer row (r+2) corresponds to y_offset + r.
            // Vertical filter center at tmp row (r + 2).
            int32 t = r + 2;
            int32 sum = 1  * tmp[(t - 2) * tmp_stride + c]
                      - 5  * tmp[(t - 1) * tmp_stride + c]
                      + 20 * tmp[(t    ) * tmp_stride + c]
                      + 20 * tmp[(t + 1) * tmp_stride + c]
                      - 5  * tmp[(t + 2) * tmp_stride + c]
                      + 1  * tmp[(t + 3) * tmp_stride + c];
            // Double shift: first >>5 from horizontal was already applied,
            // now >>5 from vertical. But the intermediate values are already
            // shifted, so we just do one more >>5 with rounding.
            int16 half = (int16)((sum + 16) >> 5);

            if (is_quarter)
            {
                int16 full = sample_clamped(plane_data, plane_stride,
                                             base_x + c, base_y + r,
                                             plane_width, plane_height);
                out_data[r * out_stride + c] = (int16)((full + half + 1) >> 1);
            }
            else
            {
                out_data[r * out_stride + c] = half;
            }
        }
    }
}

void interpolate_subpixel_6tap(const image_set &ref, int32 base_x, int32 base_y,
                                int16 dir_x, int16 dir_y, bool is_quarter,
                                macroblock *output)
{
    const image *y_img = ref.query_y_image();
    const image *u_img = ref.query_u_image();
    const image *v_img = ref.query_v_image();

    const int16 *y_data = reinterpret_cast<const int16 *>(y_img->query_data());
    const int16 *u_data = reinterpret_cast<const int16 *>(u_img->query_data());
    const int16 *v_data = reinterpret_cast<const int16 *>(v_img->query_data());

    int32 y_stride = y_img->query_row_pitch() / sizeof(int16);
    int32 c_stride = u_img->query_row_pitch() / sizeof(int16);

    int32 y_width  = y_img->query_width();
    int32 y_height = y_img->query_height();
    int32 c_width  = u_img->query_width();
    int32 c_height = u_img->query_height();

    // Luma: 16x16
    interpolate_plane_6tap(y_data, y_stride, y_width, y_height,
                           base_x, base_y, EVX_MACROBLOCK_SIZE,
                           dir_x, dir_y, is_quarter,
                           output->data_y, output->stride);

    // Chroma: 8x8 at half resolution
    interpolate_plane_6tap(u_data, c_stride, c_width, c_height,
                           base_x >> 1, base_y >> 1, EVX_MACROBLOCK_SIZE >> 1,
                           dir_x, dir_y, is_quarter,
                           output->data_u, output->stride >> 1);

    interpolate_plane_6tap(v_data, c_stride, c_width, c_height,
                           base_x >> 1, base_y >> 1, EVX_MACROBLOCK_SIZE >> 1,
                           dir_x, dir_y, is_quarter,
                           output->data_v, output->stride >> 1);
}

} // namespace evx
