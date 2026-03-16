
#include "bitstream.h"
#include "evx_math.h"
#include "evx_memory.h"

namespace evx {

bit_stream::bit_stream() 
{
    read_index = 0;
    write_index = 0;
    data_store = 0;
    data_capacity = 0;
}

bit_stream::bit_stream(uint32 size) 
{
    data_store = 0;

    if (size != resize_capacity(size)) 
    {
        evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
}

bit_stream::bit_stream(void *bytes, uint32 size) 
{
    data_store = 0;

    if (0 != assign(bytes, size)) 
    {
        evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
    }
}

bit_stream::~bit_stream() 
{
    clear();
}

uint8 *bit_stream::query_data() const 
{
    return data_store;
}

uint32 bit_stream::query_capacity() const 
{
    return data_capacity << 3;
}

uint32 bit_stream::query_occupancy() const 
{
    return write_index - read_index;
}

uint32 bit_stream::query_byte_occupancy() const
{
    return align(query_occupancy(), 8) >> 3;
}

uint32 bit_stream::query_read_index() const
{
    return read_index;
}

uint32 bit_stream::query_write_index() const
{
    return write_index;
}

uint32 bit_stream::resize_capacity(uint32 size_in_bits) 
{
    if (EVX_PARAM_CHECK) 
    {
        if (0 == size_in_bits) 
        {
            evx_post_error(EVX_ERROR_INVALIDARG);
            return 0;
        }
    }

    clear();

    uint32 byte_size = align(size_in_bits, 8) >> 3;
    data_store = new uint8[byte_size];

    if (!data_store) 
    {
        evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        return 0;
    }

    data_capacity = byte_size;
    return size_in_bits;
}

void bit_stream::seek(uint32 offset)
{
    if (read_index + offset >= write_index)
    {
        read_index = write_index;
    }
    else
    {
        read_index += offset;
    }
}

evx_status bit_stream::assign(void *bytes, uint32 size) 
{
    if (EVX_PARAM_CHECK) 
    {
        if (0 == size || !bytes) 
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    clear();

    // Copy the data into our own buffer and adjust our indices.
    data_store = new uint8[size];

    if (!data_store) 
    {
        return evx_post_error(EVX_ERROR_OUTOFMEMORY);
    }

    memcpy(data_store, bytes, size);

    read_index  = 0;
    write_index = size << 3;
    data_capacity = size;

    return EVX_SUCCESS;
}

void bit_stream::clear() 
{
    empty();

    delete [] data_store;
    data_store = 0;
    data_capacity = 0;
}

void bit_stream::empty() 
{
    write_index = 0;
    read_index = 0;
}

bool bit_stream::is_empty() const 
{
    return (write_index == read_index);
}

bool bit_stream::is_full() const 
{
    return (write_index == query_capacity());
}

evx_status bit_stream::write_byte(uint8 value) 
{
    if (write_index + 8 > query_capacity()) 
    {
        return EVX_ERROR_CAPACITY_LIMIT;
    }

    // Determine the current byte to write.
    uint32 dest_byte = write_index >> 3;
    uint8 dest_bit = write_index % 8;

    if (0 == dest_bit) 
    {
        // This is a byte aligned write, so we perform it at byte level.
        uint8 *data = &(data_store[dest_byte]);
        *data = value;
        write_index += 8;
    } 
    else 
    {
        // Slower byte unaligned write.
        for (uint8 i = 0; i < 8; ++i) 
        {
            write_bit((value >> i) & 0x1);
        }
    }

    return EVX_SUCCESS;
}

evx_status bit_stream::write_bit(uint8 value) 
{
    if (write_index + 1 > query_capacity()) 
    {
        return EVX_ERROR_CAPACITY_LIMIT;
    }

    // Determine the current byte to write.
    uint32 dest_byte = write_index >> 3;
    uint8 dest_bit = write_index % 8;

    // Pull the correct byte from our data store, update it, and then store it.
    // Note that we do not guarantee that unused buffer memory was zero filled,
    // thus we safely clear the write bit.
    uint8 *data = &(data_store[dest_byte]);
    *data = ((*data) & ~(0x1 << dest_bit)) | (value & 0x1) << dest_bit;
    write_index++;

    return EVX_SUCCESS;
}

evx_status bit_stream::write_bits(void *data, uint32 bit_count) 
{
    if (EVX_PARAM_CHECK) 
    {
        if (!data || 0 == bit_count) 
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (write_index + bit_count > query_capacity()) 
    {
        return EVX_ERROR_CAPACITY_LIMIT;
    }

    uint32 bits_copied = 0;
    uint8 *source = reinterpret_cast<uint8 *>(data);

    if (0 == (write_index % 8) && (bit_count >= 8)) 
    {
        // We can perform a (partial) fast copy because our source and destination 
        // are byte aligned. We handle any trailing bits below.
        bits_copied = aligned_bit_copy(source, 0, bit_count, data_store, write_index);

        if (!bits_copied) 
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    if (bits_copied < bit_count) 
    {
        // Perform unaligned copies of our data.
        bits_copied += unaligned_bit_copy(source, 
                                          bits_copied, 
                                          bit_count - bits_copied,
                                          data_store, 
                                          write_index + bits_copied);
    }

    write_index += bits_copied;

    return EVX_SUCCESS;
}

evx_status bit_stream::write_bytes(void *data, uint32 byte_count) 
{
    return write_bits(data, byte_count << 3);
}

evx_status bit_stream::read_bit(void *data) 
{
    evx_status result = peek_bit(data);

    if (EVX_SUCCESS == result)
    {
        read_index++;
    }

    return result;
}

evx_status bit_stream::read_byte(void *data) 
{
    evx_status result = peek_byte(data);

    if (EVX_SUCCESS == result)
    {
        read_index += 8;
    } 

    return result;
}

evx_status bit_stream::read_bits(void *data, uint32 count) 
{
    evx_status result = peek_bits(data, count);

    if (EVX_SUCCESS == result)
    {
        read_index += count;
    } 

    return result;
}

evx_status bit_stream::read_bytes(void *data, uint32 count) 
{
    return read_bits(data, count << 3);
}

evx_status bit_stream::peek_byte(void *data)
{
    if (EVX_PARAM_CHECK) 
    {
        if (!data) 
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (read_index + 8 > write_index) 
    {
        return EVX_ERROR_INVALID_RESOURCE;
    }

    // Determine the current byte to read from.
    uint32 source_byte = read_index >> 3;
    uint8 source_bit = read_index % 8;
    uint8 *dest = reinterpret_cast<uint8 *>(data);

    if (0 == (source_bit % 8)) 
    {
        *dest = data_store[source_byte];
    } 
    else
    {
        // Slower byte unaligned read.
        for (uint8 i = 0; i < 8; ++i) 
        {
            uint8 temp = 0;
            peek_bit(&temp);
            *dest = (*dest & ~(0x1 << i)) | (temp << i);
        }
    }

    return EVX_SUCCESS;
}

evx_status bit_stream::peek_bit(void *data)
{
    if (EVX_PARAM_CHECK) 
    {
        if (!data) 
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (read_index >= write_index) 
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    // Determine the current byte to read from.
    uint32 source_byte = read_index >> 3;
    uint8 source_bit = read_index % 8;
    uint8 *dest = reinterpret_cast<uint8 *>(data);

    // Pull the correct byte from our data store. Note that we 
    // preserve the high bits of *dest.
    *dest &= 0xFE;
    *dest |= ((data_store[source_byte]) >> source_bit) & 0x1;

    return EVX_SUCCESS;
}

evx_status bit_stream::peek_bytes(void *data, uint32 count)
{
    return peek_bits(data, count << 3);
}

evx_status bit_stream::peek_bits(void *data, uint32 count)
{
    if (EVX_PARAM_CHECK) 
    {
        if (!data || 0 == count) 
        {
            return evx_post_error(EVX_ERROR_INVALIDARG);
        }
    }

    if (read_index + count > write_index) 
    {
        return evx_post_error(EVX_ERROR_INVALID_RESOURCE);
    }

    uint32 bits_copied = 0;
    uint8 *dest = reinterpret_cast<uint8 *>(data);

    if (0 == (read_index % 8) && (count >= 8 )) 
    {
        // We can perform a (partial) fast copy because our source and destination 
        // are byte aligned. We handle any trailing bits below.
        bits_copied = aligned_bit_copy(data_store, read_index, count, dest, 0);

        if (!bits_copied) 
        {
            return evx_post_error(EVX_ERROR_EXECUTION_FAILURE);
        }
    }

    if (bits_copied < count) 
    {
        // Perform unaligned copies of our data.
        bits_copied += unaligned_bit_copy(data_store, 
                                          read_index + bits_copied, 
                                          count - bits_copied,
                                          dest, 
                                          bits_copied);
    }

    return EVX_SUCCESS;
}

} // namespace evx