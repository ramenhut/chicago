
#include "sao.h"
#include "config.h"
#include "macroblock.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

namespace evx {

#if EVX_ENABLE_SAO

static inline int16 sao_clamp(int32 val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (int16)val;
}

// Neighbor offset table: [eo_class][dx_a, dy_a, dx_b, dy_b]
static const int8 eo_neighbor_offsets[4][4] = {
    { -1,  0,  1,  0 },  // horizontal
    {  0, -1,  0,  1 },  // vertical
    { -1, -1,  1,  1 },  // 135 diagonal
    {  1, -1, -1,  1 },  // 45 diagonal
};

static uint8 sao_classify_edge(int16 pixel, int16 neighbor_a, int16 neighbor_b)
{
    int8 sign_a = (neighbor_a > pixel) ? 1 : (neighbor_a < pixel) ? -1 : 0;
    int8 sign_b = (neighbor_b > pixel) ? 1 : (neighbor_b < pixel) ? -1 : 0;
    int8 sum = sign_a + sign_b;

    if (sum == 2) return EVX_SAO_EO_CAT_VALLEY;
    if (sum == 1) return EVX_SAO_EO_CAT_CONCAVE;
    if (sum == -1) return EVX_SAO_EO_CAT_CONVEX;
    if (sum == -2) return EVX_SAO_EO_CAT_PEAK;
    return EVX_SAO_EO_CAT_NONE;
}

static void sao_apply_band_offset(int16 *data, uint32 pitch, uint32 x0, uint32 y0,
                                   uint32 mb_w, uint32 mb_h, const evx_sao_params &params)
{
    for (uint32 j = y0; j < y0 + mb_h; j++)
    for (uint32 i = x0; i < x0 + mb_w; i++)
    {
        int16 pixel = data[j * pitch + i];
        int32 band = sao_clamp(pixel) >> 3;
        int32 idx = band - (int32)params.band_position;
        if (idx >= 0 && idx < EVX_SAO_NUM_OFFSETS)
        {
            data[j * pitch + i] = sao_clamp(pixel + params.offsets[idx]);
        }
    }
}

static void sao_apply_edge_offset(int16 *data, uint32 pitch, uint32 x0, uint32 y0,
                                   uint32 mb_w, uint32 mb_h, uint32 img_w, uint32 img_h,
                                   const evx_sao_params &params)
{
    int8 dx_a = eo_neighbor_offsets[params.eo_class][0];
    int8 dy_a = eo_neighbor_offsets[params.eo_class][1];
    int8 dx_b = eo_neighbor_offsets[params.eo_class][2];
    int8 dy_b = eo_neighbor_offsets[params.eo_class][3];

    // Classify all pixels using original values before writing any corrections.
    // Without this, in-place writes corrupt neighbor reads for subsequent pixels.
    int8 categories[EVX_MACROBLOCK_SIZE * EVX_MACROBLOCK_SIZE];
    for (uint32 j = 0; j < mb_h; j++)
    for (uint32 i = 0; i < mb_w; i++)
    {
        int32 px = (int32)(x0 + i), py = (int32)(y0 + j);
        int32 na_x = px + dx_a, na_y = py + dy_a;
        int32 nb_x = px + dx_b, nb_y = py + dy_b;

        if (na_x < 0 || na_x >= (int32)img_w || na_y < 0 || na_y >= (int32)img_h ||
            nb_x < 0 || nb_x >= (int32)img_w || nb_y < 0 || nb_y >= (int32)img_h)
        {
            categories[j * mb_w + i] = EVX_SAO_EO_CAT_NONE;
            continue;
        }

        int16 pixel = data[py * pitch + px];
        categories[j * mb_w + i] = sao_classify_edge(pixel, data[na_y * pitch + na_x], data[nb_y * pitch + nb_x]);
    }

    // Apply offsets using pre-computed categories.
    for (uint32 j = 0; j < mb_h; j++)
    for (uint32 i = 0; i < mb_w; i++)
    {
        uint8 category = categories[j * mb_w + i];
        if (category < EVX_SAO_NUM_OFFSETS)
        {
            uint32 idx = (y0 + j) * pitch + (x0 + i);
            data[idx] = sao_clamp(data[idx] + params.offsets[category]);
        }
    }
}

void sao_filter_plane(const evx_sao_info *sao_table, uint32 width_in_blocks, uint32 height_in_blocks,
                      image *plane, uint16 mb_size, bool is_luma)
{
    int16 *data = reinterpret_cast<int16 *>(plane->query_data());
    uint32 pitch = plane->query_row_pitch() / sizeof(int16);
    uint32 img_w = plane->query_width();
    uint32 img_h = plane->query_height();

    for (uint32 by = 0; by < height_in_blocks; by++)
    for (uint32 bx = 0; bx < width_in_blocks; bx++)
    {
        uint32 mb_idx = by * width_in_blocks + bx;
        const evx_sao_info *info = &sao_table[mb_idx];

        // Resolve merge
        const evx_sao_params *params;
        if (info->merge_left && bx > 0)
            params = is_luma ? &sao_table[mb_idx - 1].luma : &sao_table[mb_idx - 1].chroma;
        else if (info->merge_above && by > 0)
            params = is_luma ? &sao_table[mb_idx - width_in_blocks].luma : &sao_table[mb_idx - width_in_blocks].chroma;
        else
            params = is_luma ? &info->luma : &info->chroma;

        if (params->type == EVX_SAO_OFF) continue;

        uint32 x0 = bx * mb_size;
        uint32 y0 = by * mb_size;
        uint32 mb_w = (x0 + mb_size > img_w) ? img_w - x0 : mb_size;
        uint32 mb_h = (y0 + mb_size > img_h) ? img_h - y0 : mb_size;

        if (params->type == EVX_SAO_BAND)
            sao_apply_band_offset(data, pitch, x0, y0, mb_w, mb_h, *params);
        else if (params->type == EVX_SAO_EDGE)
            sao_apply_edge_offset(data, pitch, x0, y0, mb_w, mb_h, img_w, img_h, *params);
    }
}

evx_status sao_filter_image(const evx_sao_info *sao_table, uint32 width_in_blocks, uint32 height_in_blocks,
                            image_set *target)
{
    sao_filter_plane(sao_table, width_in_blocks, height_in_blocks,
                     target->query_y_image(), EVX_MACROBLOCK_SIZE, true);
    sao_filter_plane(sao_table, width_in_blocks, height_in_blocks,
                     target->query_u_image(), EVX_MACROBLOCK_SIZE >> 1, false);
    sao_filter_plane(sao_table, width_in_blocks, height_in_blocks,
                     target->query_v_image(), EVX_MACROBLOCK_SIZE >> 1, false);
    return EVX_SUCCESS;
}

// --- Encoder RDO ---

static uint32 sao_estimate_bits(const evx_sao_params &params)
{
    if (params.type == EVX_SAO_OFF) return 2;
    uint32 bits = 2;  // type
    if (params.type == EVX_SAO_EDGE) bits += 2;  // eo_class
    else bits += 5;  // band_position
    for (int i = 0; i < EVX_SAO_NUM_OFFSETS; i++)
        bits += (params.offsets[i] != 0) ? 4 : 1;
    return bits;
}

static int64_t sao_compute_ssd(const int16 *src, uint32 src_pitch,
                                const int16 *rec, uint32 rec_pitch,
                                uint32 x0, uint32 y0, uint32 w, uint32 h)
{
    int64_t ssd = 0;
    for (uint32 j = y0; j < y0 + h; j++)
    for (uint32 i = x0; i < x0 + w; i++)
    {
        int32 diff = (int32)src[j * src_pitch + i] - (int32)rec[j * rec_pitch + i];
        ssd += diff * diff;
    }
    return ssd;
}

static void sao_try_band_offset(const int16 *src, uint32 src_pitch,
                                 const int16 *rec, uint32 rec_pitch,
                                 uint32 x0, uint32 y0, uint32 w, uint32 h,
                                 evx_sao_params *best, float *best_cost, float lambda)
{
    int32 band_sum[EVX_SAO_BAND_COUNT] = {};
    int32 band_count[EVX_SAO_BAND_COUNT] = {};

    for (uint32 j = y0; j < y0 + h; j++)
    for (uint32 i = x0; i < x0 + w; i++)
    {
        int16 pixel = rec[j * rec_pitch + i];
        int32 band = sao_clamp(pixel) >> 3;
        band_sum[band] += (int32)src[j * src_pitch + i] - (int32)pixel;
        band_count[band]++;
    }

    for (uint8 bp = 0; bp <= EVX_SAO_BAND_COUNT - EVX_SAO_NUM_OFFSETS; bp++)
    {
        evx_sao_params candidate;
        candidate.type = EVX_SAO_BAND;
        candidate.eo_class = 0;
        candidate.band_position = bp;

        bool has_nonzero = false;
        for (int k = 0; k < EVX_SAO_NUM_OFFSETS; k++)
        {
            int32 cnt = band_count[bp + k];
            if (cnt > 0)
            {
                int32 avg = (band_sum[bp + k] + (band_sum[bp + k] >= 0 ? cnt / 2 : -cnt / 2)) / cnt;
                if (avg > EVX_SAO_MAX_OFFSET) avg = EVX_SAO_MAX_OFFSET;
                if (avg < -EVX_SAO_MAX_OFFSET) avg = -EVX_SAO_MAX_OFFSET;
                candidate.offsets[k] = (int8)avg;
                if (avg != 0) has_nonzero = true;
            }
            else
            {
                candidate.offsets[k] = 0;
            }
        }

        if (!has_nonzero) continue;

        int64_t ssd = 0;
        for (uint32 j = y0; j < y0 + h; j++)
        for (uint32 i = x0; i < x0 + w; i++)
        {
            int16 pixel = rec[j * rec_pitch + i];
            int32 band = sao_clamp(pixel) >> 3;
            int32 idx = band - (int32)bp;
            int16 corrected = (idx >= 0 && idx < EVX_SAO_NUM_OFFSETS)
                ? sao_clamp(pixel + candidate.offsets[idx]) : pixel;
            int32 diff = (int32)src[j * src_pitch + i] - (int32)corrected;
            ssd += diff * diff;
        }

        float cost = (float)ssd + lambda * (float)sao_estimate_bits(candidate);
        if (cost < *best_cost)
        {
            *best_cost = cost;
            *best = candidate;
        }
    }
}

static void sao_try_edge_offset(const int16 *src, uint32 src_pitch,
                                 const int16 *rec, uint32 rec_pitch,
                                 uint32 x0, uint32 y0, uint32 w, uint32 h,
                                 uint32 img_w, uint32 img_h,
                                 evx_sao_params *best, float *best_cost, float lambda)
{
    for (uint8 eo_class = 0; eo_class < 4; eo_class++)
    {
        int8 dx_a = eo_neighbor_offsets[eo_class][0];
        int8 dy_a = eo_neighbor_offsets[eo_class][1];
        int8 dx_b = eo_neighbor_offsets[eo_class][2];
        int8 dy_b = eo_neighbor_offsets[eo_class][3];

        int32 cat_sum[EVX_SAO_NUM_OFFSETS] = {};
        int32 cat_count[EVX_SAO_NUM_OFFSETS] = {};

        for (uint32 j = y0; j < y0 + h; j++)
        for (uint32 i = x0; i < x0 + w; i++)
        {
            int32 na_x = (int32)i + dx_a, na_y = (int32)j + dy_a;
            int32 nb_x = (int32)i + dx_b, nb_y = (int32)j + dy_b;
            if (na_x < 0 || na_x >= (int32)img_w || na_y < 0 || na_y >= (int32)img_h) continue;
            if (nb_x < 0 || nb_x >= (int32)img_w || nb_y < 0 || nb_y >= (int32)img_h) continue;

            int16 pixel = rec[j * rec_pitch + i];
            uint8 cat = sao_classify_edge(pixel, rec[na_y * rec_pitch + na_x], rec[nb_y * rec_pitch + nb_x]);
            if (cat < EVX_SAO_NUM_OFFSETS)
            {
                cat_sum[cat] += (int32)src[j * src_pitch + i] - (int32)pixel;
                cat_count[cat]++;
            }
        }

        evx_sao_params candidate;
        candidate.type = EVX_SAO_EDGE;
        candidate.eo_class = eo_class;
        candidate.band_position = 0;

        bool has_nonzero = false;
        for (int k = 0; k < EVX_SAO_NUM_OFFSETS; k++)
        {
            if (cat_count[k] > 0)
            {
                int32 avg = (cat_sum[k] + (cat_sum[k] >= 0 ? cat_count[k] / 2 : -cat_count[k] / 2)) / cat_count[k];
                if (avg > EVX_SAO_MAX_OFFSET) avg = EVX_SAO_MAX_OFFSET;
                if (avg < -EVX_SAO_MAX_OFFSET) avg = -EVX_SAO_MAX_OFFSET;
                candidate.offsets[k] = (int8)avg;
                if (avg != 0) has_nonzero = true;
            }
            else
            {
                candidate.offsets[k] = 0;
            }
        }

        if (!has_nonzero) continue;

        int64_t ssd = 0;
        for (uint32 j = y0; j < y0 + h; j++)
        for (uint32 i = x0; i < x0 + w; i++)
        {
            int16 pixel = rec[j * rec_pitch + i];
            int16 corrected = pixel;
            int32 na_x = (int32)i + dx_a, na_y = (int32)j + dy_a;
            int32 nb_x = (int32)i + dx_b, nb_y = (int32)j + dy_b;

            if (na_x >= 0 && na_x < (int32)img_w && na_y >= 0 && na_y < (int32)img_h &&
                nb_x >= 0 && nb_x < (int32)img_w && nb_y >= 0 && nb_y < (int32)img_h)
            {
                uint8 cat = sao_classify_edge(pixel, rec[na_y * rec_pitch + na_x], rec[nb_y * rec_pitch + nb_x]);
                if (cat < EVX_SAO_NUM_OFFSETS)
                    corrected = sao_clamp(pixel + candidate.offsets[cat]);
            }

            int32 diff = (int32)src[j * src_pitch + i] - (int32)corrected;
            ssd += diff * diff;
        }

        float cost = (float)ssd + lambda * (float)sao_estimate_bits(candidate);
        if (cost < *best_cost)
        {
            *best_cost = cost;
            *best = candidate;
        }
    }
}

static void sao_rdo_macroblock_plane(const int16 *src, uint32 src_pitch,
                                      const int16 *rec, uint32 rec_pitch,
                                      uint32 x0, uint32 y0, uint32 mb_w, uint32 mb_h,
                                      uint32 img_w, uint32 img_h,
                                      float lambda, evx_sao_params *out)
{
    clear_sao_params(out);
    int64_t base_ssd = sao_compute_ssd(src, src_pitch, rec, rec_pitch, x0, y0, mb_w, mb_h);
    float best_cost = (float)base_ssd + lambda * (float)sao_estimate_bits(*out);

    sao_try_band_offset(src, src_pitch, rec, rec_pitch, x0, y0, mb_w, mb_h,
                         out, &best_cost, lambda);

    sao_try_edge_offset(src, src_pitch, rec, rec_pitch, x0, y0, mb_w, mb_h,
                         img_w, img_h, out, &best_cost, lambda);
}

// Compute SSD for a macroblock region with given SAO params applied.
static float sao_merge_cost(const int16 *src, uint32 src_pitch,
                             const int16 *rec, uint32 rec_pitch,
                             uint32 x0, uint32 y0, uint32 w, uint32 h,
                             uint32 img_w, uint32 img_h,
                             const evx_sao_params &params, float lambda)
{
    int64_t ssd = 0;
    for (uint32 j = y0; j < y0 + h; j++)
    for (uint32 i = x0; i < x0 + w; i++)
    {
        int16 pixel = rec[j * rec_pitch + i];
        int16 corrected = pixel;

        if (params.type == EVX_SAO_BAND)
        {
            int32 band = sao_clamp(pixel) >> 3;
            int32 idx = band - (int32)params.band_position;
            if (idx >= 0 && idx < EVX_SAO_NUM_OFFSETS)
                corrected = sao_clamp(pixel + params.offsets[idx]);
        }
        else if (params.type == EVX_SAO_EDGE)
        {
            int8 dx_a = eo_neighbor_offsets[params.eo_class][0];
            int8 dy_a = eo_neighbor_offsets[params.eo_class][1];
            int8 dx_b = eo_neighbor_offsets[params.eo_class][2];
            int8 dy_b = eo_neighbor_offsets[params.eo_class][3];
            int32 na_x = (int32)i + dx_a, na_y = (int32)j + dy_a;
            int32 nb_x = (int32)i + dx_b, nb_y = (int32)j + dy_b;
            if (na_x >= 0 && na_x < (int32)img_w && na_y >= 0 && na_y < (int32)img_h &&
                nb_x >= 0 && nb_x < (int32)img_w && nb_y >= 0 && nb_y < (int32)img_h)
            {
                uint8 cat = sao_classify_edge(pixel, rec[na_y * rec_pitch + na_x], rec[nb_y * rec_pitch + nb_x]);
                if (cat < EVX_SAO_NUM_OFFSETS)
                    corrected = sao_clamp(pixel + params.offsets[cat]);
            }
        }

        int32 diff = (int32)src[j * src_pitch + i] - (int32)corrected;
        ssd += diff * diff;
    }
    return (float)ssd + lambda * 1.0f;  // 1 bit for merge flag
}

evx_status sao_encode_frame(const image_set &source, image_set *reconstructed,
                            evx_sao_info *sao_table, uint32 width_in_blocks, uint32 height_in_blocks,
                            float lambda)
{
    const int16 *src_y = reinterpret_cast<const int16 *>(source.query_y_image()->query_data());
    int16 *rec_y = reinterpret_cast<int16 *>(reconstructed->query_y_image()->query_data());
    uint32 src_pitch_y = source.query_y_image()->query_row_pitch() / sizeof(int16);
    uint32 rec_pitch_y = reconstructed->query_y_image()->query_row_pitch() / sizeof(int16);
    uint32 img_w_y = reconstructed->query_y_image()->query_width();
    uint32 img_h_y = reconstructed->query_y_image()->query_height();

    const int16 *src_u = reinterpret_cast<const int16 *>(source.query_u_image()->query_data());
    int16 *rec_u = reinterpret_cast<int16 *>(reconstructed->query_u_image()->query_data());
#if EVX_ENABLE_SAO_CHROMA_MERGE
    const int16 *src_v = reinterpret_cast<const int16 *>(source.query_v_image()->query_data());
    int16 *rec_v = reinterpret_cast<int16 *>(reconstructed->query_v_image()->query_data());
#endif
    uint32 src_pitch_c = source.query_u_image()->query_row_pitch() / sizeof(int16);
    uint32 rec_pitch_c = reconstructed->query_u_image()->query_row_pitch() / sizeof(int16);
    uint32 img_w_c = reconstructed->query_u_image()->query_width();
    uint32 img_h_c = reconstructed->query_u_image()->query_height();

    uint16 mb_luma = EVX_MACROBLOCK_SIZE;
    uint16 mb_chroma = EVX_MACROBLOCK_SIZE >> 1;

    for (uint32 by = 0; by < height_in_blocks; by++)
    for (uint32 bx = 0; bx < width_in_blocks; bx++)
    {
        uint32 mb_idx = by * width_in_blocks + bx;
        evx_sao_info *info = &sao_table[mb_idx];
        clear_sao_info(info);

        uint32 lx0 = bx * mb_luma, ly0 = by * mb_luma;
        uint32 lw = (lx0 + mb_luma > img_w_y) ? img_w_y - lx0 : mb_luma;
        uint32 lh = (ly0 + mb_luma > img_h_y) ? img_h_y - ly0 : mb_luma;

        // RDO: find best luma SAO
        sao_rdo_macroblock_plane(src_y, src_pitch_y, rec_y, rec_pitch_y,
                                  lx0, ly0, lw, lh, img_w_y, img_h_y,
                                  lambda, &info->luma);

        // RDO: find best chroma SAO
        uint32 cx0 = bx * mb_chroma, cy0 = by * mb_chroma;
        uint32 cw = (cx0 + mb_chroma > img_w_c) ? img_w_c - cx0 : mb_chroma;
        uint32 ch = (cy0 + mb_chroma > img_h_c) ? img_h_c - cy0 : mb_chroma;

#if EVX_ENABLE_SAO_CHROMA_MERGE
        // RDO: find best chroma SAO from both U and V channels independently,
        // then pick whichever has lower combined U+V distortion.
        evx_sao_params chroma_u, chroma_v;
        sao_rdo_macroblock_plane(src_u, src_pitch_c, rec_u, rec_pitch_c,
                                  cx0, cy0, cw, ch, img_w_c, img_h_c,
                                  lambda, &chroma_u);
        sao_rdo_macroblock_plane(src_v, src_pitch_c, rec_v, rec_pitch_c,
                                  cx0, cy0, cw, ch, img_w_c, img_h_c,
                                  lambda, &chroma_v);
        {
            float cost_u = (float)sao_compute_ssd(src_u, src_pitch_c, rec_u, rec_pitch_c, cx0, cy0, cw, ch);
            float cost_v = (float)sao_compute_ssd(src_v, src_pitch_c, rec_v, rec_pitch_c, cx0, cy0, cw, ch);
            float base_chroma = cost_u + cost_v + lambda * 2.0f * 2;

            float cu_cost = (chroma_u.type == EVX_SAO_OFF) ? base_chroma :
                sao_merge_cost(src_u, src_pitch_c, rec_u, rec_pitch_c, cx0, cy0, cw, ch, img_w_c, img_h_c, chroma_u, lambda) +
                sao_merge_cost(src_v, src_pitch_c, rec_v, rec_pitch_c, cx0, cy0, cw, ch, img_w_c, img_h_c, chroma_u, lambda);
            float cv_cost = (chroma_v.type == EVX_SAO_OFF) ? base_chroma :
                sao_merge_cost(src_u, src_pitch_c, rec_u, rec_pitch_c, cx0, cy0, cw, ch, img_w_c, img_h_c, chroma_v, lambda) +
                sao_merge_cost(src_v, src_pitch_c, rec_v, rec_pitch_c, cx0, cy0, cw, ch, img_w_c, img_h_c, chroma_v, lambda);
            info->chroma = (cu_cost <= cv_cost) ? chroma_u : chroma_v;
        }

        // Combined luma + chroma cost
        float cur_cost;
        {
            float luma_cost;
            if (info->luma.type == EVX_SAO_OFF)
            {
                int64_t base_ssd = sao_compute_ssd(src_y, src_pitch_y, rec_y, rec_pitch_y, lx0, ly0, lw, lh);
                luma_cost = (float)base_ssd + lambda * 2.0f;
            }
            else
            {
                luma_cost = sao_merge_cost(src_y, src_pitch_y, rec_y, rec_pitch_y,
                                            lx0, ly0, lw, lh, img_w_y, img_h_y,
                                            info->luma, lambda)
                            - lambda * 1.0f + lambda * (float)sao_estimate_bits(info->luma);
            }

            float chroma_cost;
            if (info->chroma.type == EVX_SAO_OFF)
            {
                chroma_cost = (float)sao_compute_ssd(src_u, src_pitch_c, rec_u, rec_pitch_c, cx0, cy0, cw, ch)
                            + (float)sao_compute_ssd(src_v, src_pitch_c, rec_v, rec_pitch_c, cx0, cy0, cw, ch)
                            + lambda * 2.0f * 2;
            }
            else
            {
                chroma_cost = sao_merge_cost(src_u, src_pitch_c, rec_u, rec_pitch_c, cx0, cy0, cw, ch, img_w_c, img_h_c, info->chroma, lambda)
                            + sao_merge_cost(src_v, src_pitch_c, rec_v, rec_pitch_c, cx0, cy0, cw, ch, img_w_c, img_h_c, info->chroma, lambda)
                            - lambda * 2.0f + lambda * (float)sao_estimate_bits(info->chroma) * 2;
            }
            cur_cost = luma_cost + chroma_cost;
        }

        // Try merge_left (combined luma+chroma cost)
        if (bx > 0 && (sao_table[mb_idx - 1].luma.type != EVX_SAO_OFF ||
                        sao_table[mb_idx - 1].chroma.type != EVX_SAO_OFF))
        {
            const evx_sao_info *left = &sao_table[mb_idx - 1];
            float mc_luma = sao_merge_cost(src_y, src_pitch_y, rec_y, rec_pitch_y,
                                            lx0, ly0, lw, lh, img_w_y, img_h_y,
                                            left->luma, lambda);
            float mc_chroma = sao_merge_cost(src_u, src_pitch_c, rec_u, rec_pitch_c,
                                              cx0, cy0, cw, ch, img_w_c, img_h_c,
                                              left->chroma, lambda)
                            + sao_merge_cost(src_v, src_pitch_c, rec_v, rec_pitch_c,
                                              cx0, cy0, cw, ch, img_w_c, img_h_c,
                                              left->chroma, lambda);
            float mc = mc_luma + mc_chroma;
            if (mc < cur_cost)
            {
                info->merge_left = true;
                info->luma = left->luma;
                info->chroma = left->chroma;
                cur_cost = mc;
            }
        }

        // Try merge_above (combined luma+chroma cost)
        if (!info->merge_left && by > 0 &&
            (sao_table[mb_idx - width_in_blocks].luma.type != EVX_SAO_OFF ||
             sao_table[mb_idx - width_in_blocks].chroma.type != EVX_SAO_OFF))
        {
            const evx_sao_info *above = &sao_table[mb_idx - width_in_blocks];
            float mc_luma = sao_merge_cost(src_y, src_pitch_y, rec_y, rec_pitch_y,
                                            lx0, ly0, lw, lh, img_w_y, img_h_y,
                                            above->luma, lambda);
            float mc_chroma = sao_merge_cost(src_u, src_pitch_c, rec_u, rec_pitch_c,
                                              cx0, cy0, cw, ch, img_w_c, img_h_c,
                                              above->chroma, lambda)
                            + sao_merge_cost(src_v, src_pitch_c, rec_v, rec_pitch_c,
                                              cx0, cy0, cw, ch, img_w_c, img_h_c,
                                              above->chroma, lambda);
            float mc = mc_luma + mc_chroma;
            if (mc < cur_cost)
            {
                info->merge_above = true;
                info->luma = above->luma;
                info->chroma = above->chroma;
            }
        }
#else
        sao_rdo_macroblock_plane(src_u, src_pitch_c, rec_u, rec_pitch_c,
                                  cx0, cy0, cw, ch, img_w_c, img_h_c,
                                  lambda, &info->chroma);

        // Luma-only merge cost
        float cur_cost;
        if (info->luma.type == EVX_SAO_OFF)
        {
            int64_t base_ssd = sao_compute_ssd(src_y, src_pitch_y, rec_y, rec_pitch_y, lx0, ly0, lw, lh);
            cur_cost = (float)base_ssd + lambda * 2.0f;
        }
        else
        {
            cur_cost = sao_merge_cost(src_y, src_pitch_y, rec_y, rec_pitch_y,
                                       lx0, ly0, lw, lh, img_w_y, img_h_y,
                                       info->luma, lambda)
                       - lambda * 1.0f + lambda * (float)sao_estimate_bits(info->luma);
        }

        // Try merge_left
        if (bx > 0 && sao_table[mb_idx - 1].luma.type != EVX_SAO_OFF)
        {
            float mc = sao_merge_cost(src_y, src_pitch_y, rec_y, rec_pitch_y,
                                       lx0, ly0, lw, lh, img_w_y, img_h_y,
                                       sao_table[mb_idx - 1].luma, lambda);
            if (mc < cur_cost)
            {
                info->merge_left = true;
                info->luma = sao_table[mb_idx - 1].luma;
                info->chroma = sao_table[mb_idx - 1].chroma;
                cur_cost = mc;
            }
        }

        // Try merge_above
        if (!info->merge_left && by > 0 && sao_table[mb_idx - width_in_blocks].luma.type != EVX_SAO_OFF)
        {
            float mc = sao_merge_cost(src_y, src_pitch_y, rec_y, rec_pitch_y,
                                       lx0, ly0, lw, lh, img_w_y, img_h_y,
                                       sao_table[mb_idx - width_in_blocks].luma, lambda);
            if (mc < cur_cost)
            {
                info->merge_above = true;
                info->luma = sao_table[mb_idx - width_in_blocks].luma;
                info->chroma = sao_table[mb_idx - width_in_blocks].chroma;
            }
        }
#endif
    }

    // Apply chosen SAO to reconstructed frame
    sao_filter_image(sao_table, width_in_blocks, height_in_blocks, reconstructed);

    return EVX_SUCCESS;
}

#endif // EVX_ENABLE_SAO

} // namespace evx
