
#include "convert.h"
#include "config.h"

namespace evx {

#define EVX_LUMINANCE_SHIFT                         (16) 
#define EVX_CHROMINANCE_SHIFT                       (128) 
          
#if EVX_ENABLE_CHROMA_SUPPORT
#define EVX_CONVERT_PIXEL_RGB_TO_YUV(r, g, b, y, u, v)                                                                 \
    y  = (((77 * ((int16) r) + 150 * ((int16) g) +  29 * ((int16) b) + 128) >> 8) + EVX_LUMINANCE_SHIFT);              \
    u += (((-43 * ((int16) r) - 85  * ((int16) g) + 128 * ((int16) b) + 128) / 256) + EVX_CHROMINANCE_SHIFT);          \
    v += (((128 * ((int16) r) - 107 * ((int16) g) -  21 * ((int16) b) + 128) / 256) + EVX_CHROMINANCE_SHIFT);

#define EVX_CONVERT_PIXEL_YUV_TO_RGB(y, u, v, r, g, b)                                                                                                                 \
    r = saturate((256 * ((int32) y - EVX_LUMINANCE_SHIFT) + 358 * (((int32) v) - EVX_CHROMINANCE_SHIFT) + 128) >> 8);                                                  \
    g = saturate((256 * ((int32) y - EVX_LUMINANCE_SHIFT) - 88  * (((int32) u) - EVX_CHROMINANCE_SHIFT) - 182 * (((int32) v) - EVX_CHROMINANCE_SHIFT) + 128) >> 8);    \
    b = saturate((256 * ((int32) y - EVX_LUMINANCE_SHIFT) + 452 * (((int32) u) - EVX_CHROMINANCE_SHIFT) + 128) >> 8); 
#else
#define EVX_CONVERT_PIXEL_RGB_TO_YUV(r, g, b, y, u, v)                                                                   \
    y  = (((77 * ((int16) r) + 150 * ((int16) g) +  29 * ((int16) b) + 128) >> 8) + EVX_LUMINANCE_SHIFT);              

#define EVX_CONVERT_PIXEL_YUV_TO_RGB(y, u, v, r, g, b)                                                                   \
    r = saturate((256 * ((int32) y - EVX_LUMINANCE_SHIFT) + 128) >> 8);                                                  \
    g = r;                                                                                                               \
    b = r; 
#endif

static inline evx_status convert_line_rgb_to_yuv_alpha(uint8 *src, uint32 length, int16 *dest_y, int16 *dest_u, int16 *dest_v)
{
    for (uint32 i = 0; i < length; i += 2)
    {   
        *dest_u = *dest_v = 0;

        EVX_CONVERT_PIXEL_RGB_TO_YUV(src[0], src[1], src[2], *dest_y, *dest_u, *dest_v);

        dest_y++;
        src += 3;

        EVX_CONVERT_PIXEL_RGB_TO_YUV(src[0], src[1], src[2], *dest_y, *dest_u, *dest_v);

        dest_y++;
        dest_u++;
        dest_v++;
        src += 3;
    }

    return EVX_SUCCESS;
}

static inline evx_status convert_line_rgb_to_yuv_beta(uint8 *src, uint32 length, int16 *dest_y, int16 *dest_u, int16 *dest_v)
{
    for (uint32 i = 0; i < length; i += 2)
    {       
        EVX_CONVERT_PIXEL_RGB_TO_YUV(src[0], src[1], src[2], *dest_y, *dest_u, *dest_v);

        dest_y++;
        src += 3;

        EVX_CONVERT_PIXEL_RGB_TO_YUV(src[0], src[1], src[2], *dest_y, *dest_u, *dest_v);

        (*dest_u) = (*dest_u + 2) >> 2;
        (*dest_v) = (*dest_v + 2) >> 2; 

        dest_y++;
        dest_u++;
        dest_v++;
        src += 3;
    }

    return EVX_SUCCESS;
}

static inline evx_status convert_line_yuv_to_rgb(int16 *src_y, int16 *src_u, int16 *src_v, uint32 length, uint8 *dest_rgb)
{
    for (uint32 i = 0; i < length; i += 2)
    {
        EVX_CONVERT_PIXEL_YUV_TO_RGB(*src_y, *src_u, *src_v, dest_rgb[0], dest_rgb[1], dest_rgb[2]);

        src_y++;
        dest_rgb += 3;

        EVX_CONVERT_PIXEL_YUV_TO_RGB(*src_y, *src_u, *src_v, dest_rgb[0], dest_rgb[1], dest_rgb[2]);

        src_y++;
        src_u++;
        src_v++;
        dest_rgb += 3;
    }

    return EVX_SUCCESS;
}

evx_status convert_image(const image &src_rgb, image *dest_y, image *dest_u, image *dest_v)
{
    if (EVX_PARAM_CHECK)
    {
        if (!dest_y || !dest_u || !dest_v)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }

        if ( src_rgb.query_image_format() != EVX_IMAGE_FORMAT_R8G8B8 ||
             dest_y->query_image_format() != EVX_IMAGE_FORMAT_R16S || 
             dest_u->query_image_format() != EVX_IMAGE_FORMAT_R16S || 
             dest_v->query_image_format() != EVX_IMAGE_FORMAT_R16S )
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    // convert_image will not perform any allocations, and will crop the image if the 
    // destination lacks sufficient space along any plane.

    uint32 width = 0;
    width = evx_min2(src_rgb.query_width(), dest_y->query_width());
    width = evx_min2(width, dest_u->query_width() << 1);
    width = evx_min2(width, dest_v->query_width() << 1);

    uint32 height = 0;
    height = evx_min2(src_rgb.query_height(), dest_y->query_height());
    height = evx_min2(height, dest_u->query_height() << 1);
    height = evx_min2(height, dest_v->query_height() << 1);

    if (width % 2 || height % 2)
    {
        // We only support images with even dimensions.
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }
    
    // Traverse the image in rows of two. The final row should be processed 
    // by the even processor but with a shift of 1 instead of 2.

    uint8 * src_data = src_rgb.query_data();
    int16 * dest_data_y = reinterpret_cast<int16 *>(dest_y->query_data());
    int16 * dest_data_u = reinterpret_cast<int16 *>(dest_u->query_data());
    int16 * dest_data_v = reinterpret_cast<int16 *>(dest_v->query_data());

    // We perform chroma sub-sampling inline with our conversion. While this improves performance 
    // for cpu based implementations, all gpu variants should first convert to 4:2:2 and then 
    // downsample the chrominance to 4:2:0 using a simple bilinear filter.

    for (uint32 i = 0; i < height; i += 2)
    {
        convert_line_rgb_to_yuv_alpha(src_data, width, dest_data_y, dest_data_u, dest_data_v);

        src_data += src_rgb.query_row_pitch();
        dest_data_y += dest_y->query_width();

        convert_line_rgb_to_yuv_beta(src_data, width, dest_data_y, dest_data_u, dest_data_v);

        src_data += src_rgb.query_row_pitch();
        dest_data_y += dest_y->query_width();
        dest_data_u += dest_u->query_width();
        dest_data_v += dest_v->query_width();
    }

    return EVX_SUCCESS;
}

evx_status convert_image(const image &src_y, const image &src_u, const image &src_v, image *dest_rgb)
{
    if (EVX_PARAM_CHECK)
    {
        if (!dest_rgb)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }

        if ( dest_rgb->query_image_format() != EVX_IMAGE_FORMAT_R8G8B8 ||
             src_y.query_image_format() != EVX_IMAGE_FORMAT_R16S || 
             src_u.query_image_format() != EVX_IMAGE_FORMAT_R16S || 
             src_v.query_image_format() != EVX_IMAGE_FORMAT_R16S )
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    // convert_image will not perform any allocations, and will crop the image if the 
    // destination lacks sufficient space along any plane.

    uint32 width = 0;
    width = evx_min2(dest_rgb->query_width(), src_y.query_width());
    width = evx_min2(width, src_u.query_width() << 1 );
    width = evx_min2(width, src_v.query_width() << 1 );

    uint32 height = 0;
    height = evx_min2(dest_rgb->query_height(), src_y.query_height());
    height = evx_min2(height, src_u.query_height() << 1);
    height = evx_min2(height, src_v.query_height() << 1);

    if (width % 2 || height % 2)
    {
        // We only support images with even dimensions.
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    // Traverse the image in rows of two. The final row should be processed 
    // by the even processor but with a shift of 1 instead of 2.

    int16 * src_data_y = reinterpret_cast<int16 *>(src_y.query_data());
    int16 * src_data_u = reinterpret_cast<int16 *>(src_u.query_data());
    int16 * src_data_v = reinterpret_cast<int16 *>(src_v.query_data());
    uint8 * dest_data_rgb = dest_rgb->query_data();

    for (uint32 i = 0; i < height; i += 2)
    {
        convert_line_yuv_to_rgb(src_data_y, src_data_u, src_data_v, width, dest_data_rgb);

        dest_data_rgb += dest_rgb->query_row_pitch();
        src_data_y += src_y.query_width();

        convert_line_yuv_to_rgb(src_data_y, src_data_u, src_data_v, width, dest_data_rgb);

        dest_data_rgb += dest_rgb->query_row_pitch();
        src_data_y += src_y.query_width();
        src_data_u += src_u.query_width();
        src_data_v += src_v.query_width();
    }

    return EVX_SUCCESS;
}

evx_status convert_image(const image &src_rgb, image_set *dest_yuv)
{
    return convert_image(src_rgb, dest_yuv->query_y_image(), dest_yuv->query_u_image(), dest_yuv->query_v_image());
}

evx_status convert_image(const image_set &src_yuv, image *dest_rgb)
{
    return convert_image(*src_yuv.query_y_image(), *src_yuv.query_u_image(), *src_yuv.query_v_image(), dest_rgb);
}

} // namespace evx