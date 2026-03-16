
#include "image.h"

namespace evx {
   
uint8 channel_count_from_format(EVX_IMAGE_FORMAT format)
{
    switch (format) 
    {
        case EVX_IMAGE_FORMAT_NONE: return 0;
        case EVX_IMAGE_FORMAT_R8G8B8: return 3;
        case EVX_IMAGE_FORMAT_R8:
        case EVX_IMAGE_FORMAT_R16S: return 1;
    }

    evx_post_error(EVX_ERROR_INVALIDARG);

    return 0;
}

uint8 pixel_rate_from_format(EVX_IMAGE_FORMAT format)
{
    switch (format) 
    {
        case EVX_IMAGE_FORMAT_NONE: return 0;
        case EVX_IMAGE_FORMAT_R8G8B8: return 24;
        case EVX_IMAGE_FORMAT_R8: return 8;
        case EVX_IMAGE_FORMAT_R16S: return 16;
    }

    evx_post_error(EVX_ERROR_INVALIDARG);

    return 0;
}

image::image()
{
    image_format = EVX_IMAGE_FORMAT_NONE;
    placement_allocation = false;
    width_in_pixels = 0;
    height_in_pixels = 0;
    bits_per_pixel = 0;
    channel_count = 0;
    data_buffer = 0;
}

image::~image()
{
    // We retain tight control of allocation and deallocation because the underlying 
    // image format is still in flux. Be sure to use create_image and destroy_image.

    deallocate();
}

uint32 image::query_row_pitch() const
{
    return (width_in_pixels * bits_per_pixel) >> 3;
}

uint32 image::query_slice_pitch() const
{
    return query_row_pitch() * height_in_pixels;
}

uint32 image::query_block_offset(uint32 i, uint32 j) const
{
    return (query_row_pitch() * j) + ((i * bits_per_pixel) >> 3);
}

evx_status image::allocate(uint32 size)
{
    if (EVX_PARAM_CHECK)
    {
        if (0 == size)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    deallocate();

    data_buffer = new uint8[size];

    if (!data_buffer)
    {
        return evx_post_error(EVX_ERROR_OUTOFMEMORY);
    }

    memset(data_buffer, 0, size);

    placement_allocation = false;

    return EVX_SUCCESS;
}

evx_status image::set_placement(void *data)
{
    if (EVX_PARAM_CHECK)
    {
        if (0 == data)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    deallocate();

    data_buffer = static_cast<uint8 *>(data);

    placement_allocation = true;
    
    return EVX_SUCCESS;
}

void image::deallocate()
{
    if (!placement_allocation)
    {
        delete [] data_buffer;
    }

    data_buffer = 0;
}

evx_status image::set_dimension(uint32 width, uint32 height)
{
    if (EVX_PARAM_CHECK)
    {
        if (0 == width || 0 == height)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    // Check for an uninitialized image.
    if (0 == bits_per_pixel || EVX_IMAGE_FORMAT_NONE == image_format)
    {
        // You must call set_image_format prior to calling this function, so that
        // we know how to allocate the image.

        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    width_in_pixels = width;
    height_in_pixels = height;

    return EVX_SUCCESS;
}

evx_status image::set_image_format(EVX_IMAGE_FORMAT format)
{
    if (EVX_PARAM_CHECK)
    {
        if (0 == channel_count_from_format(format))
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    uint32 rate_total = pixel_rate_from_format(format);
    uint8 channel_total = channel_count_from_format(format);

    if (0 != (rate_total % 8))
    {
        // The format is invalid -- it does not contain a byte aligned pixel rate.
        return evx_post_error(EVX_ERROR_INVALIDARG);
    }

    image_format = format;
    bits_per_pixel = rate_total;
    channel_count = channel_total;

    return EVX_SUCCESS;
}

uint32 image::query_width() const
{
    return width_in_pixels;
}

uint32 image::query_height() const
{
    return height_in_pixels;
}

uint8 *image::query_data() const
{
    return data_buffer;
}

uint8 image::query_bits_per_pixel() const
{
    return bits_per_pixel;
}

EVX_IMAGE_FORMAT image::query_image_format() const
{
    return image_format;
}

uint8 image::query_channel_count() const
{
    return channel_count;
}

evx_status create_image(EVX_IMAGE_FORMAT format, uint32 width, uint32 height, image *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (0 == width || 0 == height)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }

        if (!output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (EVX_SUCCESS != output->set_image_format(format))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != output->set_dimension(width, height))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    // All images are required to use byte aligned pixel rates, so there is 
    // no need to align the allocation size.

    if (EVX_SUCCESS != output->allocate((width * height * output->query_bits_per_pixel()) >> 3))
    {
        return evx_post_error(EVX_ERROR_OUTOFMEMORY);
    }

    return EVX_SUCCESS;
}

evx_status create_image(EVX_IMAGE_FORMAT format, void *image_data, uint32 width, uint32 height, image *output)
{
    if (EVX_PARAM_CHECK)
    {
        if (0 == width || 0 == height)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }

        if (!image_data || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (EVX_SUCCESS != output->set_image_format(format))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != output->set_dimension(width, height))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != output->set_placement(image_data))
    {
        return evx_post_error(EVX_ERROR_OUTOFMEMORY);
    }

    return EVX_SUCCESS;
}

evx_status destroy_image(image *input)
{
    if (EVX_PARAM_CHECK) 
    {
        if (!input)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    input->deallocate();

    return EVX_SUCCESS;
}

} // namespace evx