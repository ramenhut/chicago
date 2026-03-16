
#include "evx3dec.h"
#include "config.h"
#include "macroblock.h"

namespace evx {

evx_status engine_decode_frame(bit_stream *input, const evx_frame &frame, evx_context *context, image *output);

evx3_decoder_impl::evx3_decoder_impl()
{
    initialized = false;

    clear_frame(&frame);
    clear_header(&header);

#if EVX_ENABLE_B_FRAMES
    display_buffer_size = 0;
    next_display_order = 0;
    output_ready = false;
#endif
}

evx3_decoder_impl::~evx3_decoder_impl()
{
    if (EVX_SUCCESS != clear())
    {
        evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
}

evx_status evx3_decoder_impl::clear()
{
    if (!initialized)
    {
        return EVX_SUCCESS;
    }

    clear_frame(&frame);
    clear_context(&context);

    initialized = false;

#if EVX_ENABLE_B_FRAMES
    display_buffer_size = 0;
    next_display_order = 0;
    output_ready = false;
    for (uint32 s = 0; s < EVX_B_FRAME_COUNT + 2; s++)
    {
        display_buffer[s].valid = false;
        display_buffer[s].pixels.clear();
    }
#endif

    return EVX_SUCCESS;
}

evx_status evx3_decoder_impl::initialize(bit_stream *input)
{
    if (initialized)
    {
        // The decoder cannot be re-initialized unless it is first cleared.
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    // Read our header out of the stream.
    input->read_bytes(&header, sizeof(evx_header));

    if (evx_failed(verify_header(header)))
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    // Initialize image resources.
    uint32 aligned_width = align(header.frame_width, EVX_MACROBLOCK_SIZE);
    uint32 aligned_height = align(header.frame_height, EVX_MACROBLOCK_SIZE);

    if (EVX_SUCCESS != initialize_context(aligned_width, aligned_height, &context))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    initialized = true;

    return EVX_SUCCESS;
}

evx_status evx3_decoder_impl::read_frame_desc(bit_stream *input)
{
    evx_frame incoming_frame;

    input->read_bytes(&incoming_frame, sizeof(evx_frame));
    
    if (incoming_frame.index != frame.index)
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    frame = incoming_frame;

    return EVX_SUCCESS;
}

evx_status evx3_decoder_impl::decode(bit_stream *input, void *output)
{
    // Decode's job is to setup the pipeline and ensure that the incoming
    // frame matches the expected dimensions.

    if (EVX_PARAM_CHECK)
    {
        if (!input || !output)
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (!initialized)
    {
        if (evx_failed(initialize(input)))
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
    // B-frame streams use a 4-byte payload size prefix before each frame's data.
    // This allows exact frame boundary parsing despite ABAC coder lookahead.
    uint32 payload_size = 0;
    if (evx_failed(input->read_bytes(&payload_size, sizeof(uint32))))
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

    // Decode from exactly payload_size bytes to avoid ABAC bleeding.
    bit_stream frame_stream;
    frame_stream.resize_capacity(payload_size * 8 + 64);  // small extra for safety

    // Copy exactly payload_size bytes into frame_stream, then advance input.
    uint8 *src_data = input->query_data() + (input->query_read_index() >> 3);
    if (evx_failed(frame_stream.write_bytes(src_data, payload_size)))
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

    // Advance input read position past this frame's payload.
    input->seek(payload_size * 8);

    // Read our frame descriptor and configure our state for this frame.
    if (evx_failed(read_frame_desc(&frame_stream)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != decode_frame(&frame_stream, output))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
#else
    // Read our frame descriptor and configure our state for this frame.
    if (evx_failed(read_frame_desc(input)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    if (EVX_SUCCESS != decode_frame(input, output))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
#endif

#if EVX_ENABLE_B_FRAMES
    if (EVX_B_FRAME_COUNT > 0)
    {
        uint32 frame_bytes = header.frame_width * header.frame_height * 3;

        // Find a free slot in the display reorder buffer.
        uint32 slot = display_buffer_size;
        for (uint32 s = 0; s < display_buffer_size; s++)
        {
            if (!display_buffer[s].valid)
            {
                slot = s;
                break;
            }
        }

        if (slot >= EVX_B_FRAME_COUNT + 2)
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);

        display_buffer[slot].pixels.resize(frame_bytes);
        memcpy(display_buffer[slot].pixels.data(), output, frame_bytes);
        display_buffer[slot].display_order = frame.display_order;
        display_buffer[slot].valid = true;
        if (slot >= display_buffer_size) display_buffer_size = slot + 1;

        // Emit the next expected display-order frame if it is ready.
        output_ready = false;
        for (uint32 s = 0; s < display_buffer_size; s++)
        {
            if (display_buffer[s].valid && display_buffer[s].display_order == next_display_order)
            {
                memcpy(output, display_buffer[s].pixels.data(), frame_bytes);
                display_buffer[s].valid = false;
                next_display_order++;
                output_ready = true;
                break;
            }
        }
    }
    else
    {
        output_ready = true;
    }
#endif

    frame.index++;

    return EVX_SUCCESS;
}

evx_status evx3_decoder_impl::decode_frame(bit_stream *input, void *output)
{
    image output_image;

    if (evx_failed(create_image(EVX_IMAGE_FORMAT_R8G8B8, output, header.frame_width, header.frame_height, &output_image)))
    {
        return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }

    return engine_decode_frame(input, frame, &context, &output_image);
}

#if EVX_ENABLE_B_FRAMES

bool evx3_decoder_impl::has_output()
{
    if (EVX_B_FRAME_COUNT == 0) return true;
    return output_ready;
}

evx_status evx3_decoder_impl::flush(void *output)
{
    if (EVX_B_FRAME_COUNT == 0) return EVX_SUCCESS;

    uint32 frame_bytes = header.frame_width * header.frame_height * 3;

    for (uint32 s = 0; s < display_buffer_size; s++)
    {
        if (display_buffer[s].valid && display_buffer[s].display_order == next_display_order)
        {
            memcpy(output, display_buffer[s].pixels.data(), frame_bytes);
            display_buffer[s].valid = false;
            next_display_order++;
            output_ready = true;
            return EVX_SUCCESS;
        }
    }

    output_ready = false;
    return EVX_SUCCESS;
}

#endif

} // namespace evx