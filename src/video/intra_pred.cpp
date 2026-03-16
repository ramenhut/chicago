
#include "intra_pred.h"
#include "analysis.h"
#include "config.h"
#include "evx_math.h"

namespace evx {

// ---------------------------------------------------------------------------
// Neighbor extraction helpers
// ---------------------------------------------------------------------------

static void extract_neighbors(const image_set &recon, int32 px, int32 py,
                              int N, int16 *top, int16 *left, int16 *corner,
                              bool *has_top, bool *has_left,
                              image *img, int32 cpx, int32 cpy)
{
    int16 *data = reinterpret_cast<int16 *>(img->query_data());
    uint32 stride = img->query_row_pitch() >> 1;

    *has_top = (cpy > 0);
    *has_left = (cpx > 0);

    if (*has_top)
    {
        for (int i = 0; i < N; i++)
            top[i] = data[(cpy - 1) * stride + cpx + i];
    }
    else
    {
        for (int i = 0; i < N; i++)
            top[i] = 128;
    }

    if (*has_left)
    {
        for (int i = 0; i < N; i++)
            left[i] = data[(cpy + i) * stride + cpx - 1];
    }
    else
    {
        for (int i = 0; i < N; i++)
            left[i] = 128;
    }

    if (*has_top && *has_left)
        *corner = data[(cpy - 1) * stride + cpx - 1];
    else if (*has_top)
        *corner = top[0];
    else if (*has_left)
        *corner = left[0];
    else
        *corner = 128;
}

// ---------------------------------------------------------------------------
// Per-plane prediction: operates on an NxN block (N=16 for Y, N=8 for U/V)
// ---------------------------------------------------------------------------

static void predict_block(uint8 mode, const int16 *top, const int16 *left, int16 corner,
                           bool has_top, bool has_left, int N, int16 *out, uint32 stride)
{
    switch (mode)
    {
        case EVX_INTRA_MODE_DC:
        {
            int32 sum = 0;
            int count = 0;
            if (has_top)  { for (int i = 0; i < N; i++) sum += top[i];  count += N; }
            if (has_left) { for (int i = 0; i < N; i++) sum += left[i]; count += N; }
            int16 dc = (count > 0) ? (int16)((sum + count / 2) / count) : 128;
            for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                out[y * stride + x] = dc;
        } break;

        case EVX_INTRA_MODE_VERT:
        {
            for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                out[y * stride + x] = top[x];
        } break;

        case EVX_INTRA_MODE_HORIZ:
        {
            for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                out[y * stride + x] = left[y];
        } break;

        case EVX_INTRA_MODE_DIAG_DL:
        {
            for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
            {
                int idx = x + y;
                int32 a = top[evx_min2(idx, N - 1)];
                int32 b = top[evx_min2(idx + 1, N - 1)];
                int32 c = top[evx_min2(idx + 2, N - 1)];
                out[y * stride + x] = (int16)((a + 2 * b + c + 2) >> 2);
            }
        } break;

        case EVX_INTRA_MODE_DIAG_DR:
        {
            // Build unified diagonal reference: left[N-1]..left[0], corner, top[0]..top[N-1]
            int16 ref[33]; // max 2*16+1
            for (int i = 0; i < N; i++) ref[i] = left[N - 1 - i];
            ref[N] = corner;
            for (int i = 0; i < N; i++) ref[N + 1 + i] = top[i];

            for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
            {
                int d = x - y + N;
                out[y * stride + x] = (int16)((ref[d - 1] + 2 * ref[d] + ref[d + 1] + 2) >> 2);
            }
        } break;

        case EVX_INTRA_MODE_VERT_R:
        {
            // Shallow vertical angle: like diag-dr but with half the vertical shift.
            int16 ref[33];
            for (int i = 0; i < N; i++) ref[i] = left[N - 1 - i];
            ref[N] = corner;
            for (int i = 0; i < N; i++) ref[N + 1 + i] = top[i];

            for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
            {
                int base = x - (y >> 1);
                int d = base + N;
                d = evx_max2(d, 1);
                d = evx_min2(d, 2 * N - 1);

                if (y & 1)
                    out[y * stride + x] = (int16)((ref[d - 1] + 2 * ref[d] + ref[d + 1] + 2) >> 2);
                else
                    out[y * stride + x] = (int16)((ref[d] + ref[d + 1] + 1) >> 1);
            }
        } break;

        case EVX_INTRA_MODE_HORIZ_D:
        {
            // Shallow horizontal angle: like diag-dr but with half the horizontal shift.
            int16 ref[33];
            for (int i = 0; i < N; i++) ref[i] = left[N - 1 - i];
            ref[N] = corner;
            for (int i = 0; i < N; i++) ref[N + 1 + i] = top[i];

            for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
            {
                int base = (x >> 1) - y;
                int d = base + N;
                d = evx_max2(d, 1);
                d = evx_min2(d, 2 * N - 1);

                if (x & 1)
                    out[y * stride + x] = (int16)((ref[d - 1] + 2 * ref[d] + ref[d + 1] + 2) >> 2);
                else
                    out[y * stride + x] = (int16)((ref[d] + ref[d + 1] + 1) >> 1);
            }
        } break;

        default:
        {
            // EVX_INTRA_MODE_NONE: fill with 128 (should not be called for this mode)
            for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++)
                out[y * stride + x] = 128;
        } break;
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void generate_intra_prediction(uint8 mode, const image_set &recon,
                                int32 px, int32 py, macroblock *pred)
{
    int16 top_y[16], left_y[16], corner_y;
    int16 top_u[8], left_u[8], corner_u;
    int16 top_v[8], left_v[8], corner_v;
    bool has_top_y, has_left_y;
    bool has_top_c, has_left_c;

    // Extract Y neighbors (16x16 at pixel position px, py)
    extract_neighbors(recon, px, py, 16, top_y, left_y, &corner_y,
                      &has_top_y, &has_left_y,
                      recon.query_y_image(), px, py);

    // Extract U neighbors (8x8 at chroma position px/2, py/2)
    int32 cpx = px >> 1;
    int32 cpy = py >> 1;
    extract_neighbors(recon, px, py, 8, top_u, left_u, &corner_u,
                      &has_top_c, &has_left_c,
                      recon.query_u_image(), cpx, cpy);

    // Extract V neighbors
    extract_neighbors(recon, px, py, 8, top_v, left_v, &corner_v,
                      &has_top_c, &has_left_c,
                      recon.query_v_image(), cpx, cpy);

    // Generate Y prediction (16x16)
    predict_block(mode, top_y, left_y, corner_y, has_top_y, has_left_y,
                  16, pred->data_y, pred->stride);

    // Generate U prediction (8x8)
    predict_block(mode, top_u, left_u, corner_u, has_top_c, has_left_c,
                  8, pred->data_u, pred->stride >> 1);

    // Generate V prediction (8x8)
    predict_block(mode, top_v, left_v, corner_v, has_top_c, has_left_c,
                  8, pred->data_v, pred->stride >> 1);
}

uint8 select_best_intra_mode(const macroblock &source, const image_set &recon,
                              int32 px, int32 py, macroblock *scratch, int32 *out_sad)
{
    int32 best_sad = EVX_MAX_INT32;
    uint8 best_mode = EVX_INTRA_MODE_DC;

#if EVX_ENABLE_FAST_INTRA_MODE
    // Compute boundary gradients to determine dominant edge direction.
    int16 top_y[16], left_y[16], corner_y;
    bool has_top, has_left;
    extract_neighbors(recon, px, py, 16, top_y, left_y, &corner_y,
                      &has_top, &has_left, recon.query_y_image(), px, py);

    int32 gh = 0, gv = 0;
    if (has_top)
        for (int i = 1; i < 16; i++) gh += abs(top_y[i] - top_y[i - 1]);
    if (has_left)
        for (int i = 1; i < 16; i++) gv += abs(left_y[i] - left_y[i - 1]);

    // Select 3 candidate modes based on gradient ratio.
    uint8 candidates[3];
    if (gv > gh * 3 / 2)
    {
        // Strong vertical edges -> horizontal-leaning modes predict well.
        candidates[0] = EVX_INTRA_MODE_DC;
        candidates[1] = EVX_INTRA_MODE_HORIZ;
        candidates[2] = EVX_INTRA_MODE_HORIZ_D;
    }
    else if (gh > gv * 3 / 2)
    {
        // Strong horizontal edges -> vertical-leaning modes predict well.
        candidates[0] = EVX_INTRA_MODE_DC;
        candidates[1] = EVX_INTRA_MODE_VERT;
        candidates[2] = EVX_INTRA_MODE_VERT_R;
    }
    else
    {
        // Mixed/uniform -> diagonal modes.
        candidates[0] = EVX_INTRA_MODE_DC;
        candidates[1] = EVX_INTRA_MODE_DIAG_DL;
        candidates[2] = EVX_INTRA_MODE_DIAG_DR;
    }

    for (int c = 0; c < 3; c++)
    {
        generate_intra_prediction(candidates[c], recon, px, py, scratch);
        int32 sad = compute_block_sad(source, *scratch);
        if (sad < best_sad)
        {
            best_sad = sad;
            best_mode = candidates[c];
        }
    }
#else
    for (uint8 mode = 0; mode < EVX_INTRA_MODE_COUNT; mode++)
    {
        generate_intra_prediction(mode, recon, px, py, scratch);
        int32 sad = compute_block_sad(source, *scratch);
        if (sad < best_sad)
        {
            best_sad = sad;
            best_mode = mode;
        }
    }
#endif

    if (out_sad)
        *out_sad = best_sad;

    return best_mode;
}

uint8 select_intra_mode_candidates(const image_set &recon, int32 px, int32 py, uint8 *candidates)
{
    int16 top_y[16], left_y[16], corner_y;
    bool has_top, has_left;
    extract_neighbors(recon, px, py, 16, top_y, left_y, &corner_y,
                      &has_top, &has_left, recon.query_y_image(), px, py);

    int32 gh = 0, gv = 0;
    if (has_top)
        for (int i = 1; i < 16; i++) gh += abs(top_y[i] - top_y[i - 1]);
    if (has_left)
        for (int i = 1; i < 16; i++) gv += abs(left_y[i] - left_y[i - 1]);

    if (gv > gh * 3 / 2)
    {
        candidates[0] = EVX_INTRA_MODE_DC;
        candidates[1] = EVX_INTRA_MODE_HORIZ;
        candidates[2] = EVX_INTRA_MODE_HORIZ_D;
    }
    else if (gh > gv * 3 / 2)
    {
        candidates[0] = EVX_INTRA_MODE_DC;
        candidates[1] = EVX_INTRA_MODE_VERT;
        candidates[2] = EVX_INTRA_MODE_VERT_R;
    }
    else
    {
        candidates[0] = EVX_INTRA_MODE_DC;
        candidates[1] = EVX_INTRA_MODE_DIAG_DL;
        candidates[2] = EVX_INTRA_MODE_DIAG_DR;
    }

    return 3;
}

} // namespace evx
