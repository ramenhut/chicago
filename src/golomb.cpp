
#include "golomb.h"
#include "egtables.h"
#include "evx_math.h"

namespace evx {

uint16 encode_unsigned_golomb_value(uint8 input, uint8 *count) 
{
    uint16 result = EVX_UEXP_GOLOMB_CODES[input];

    if (count) 
    {
        *count = EVX_UEXP_GOLOMB_SIZE_LUT[input];
    }

    return result;
}

uint32 encode_signed_golomb_value(int8 input, uint8 *count) 
{
    uint8 index = input;
    uint32 result = EVX_SEXP_GOLOMB_CODES[index];

    if (count) 
    {
        *count = EVX_SEXP_GOLOMB_SIZE_LUT[index];
    }

    return result;
}

uint32 encode_unsigned_golomb_value(uint16 input, uint8 * count) 
{
    if (input < 256)
    {
        return encode_unsigned_golomb_value((uint8) input, count);
    }
 
    uint32 result = 0;
    uint32 value = input + 1;
    uint8 bit_count = evx_required_bits(value);

    while (value) 
    {
        result <<= 1;
        result |= value & 0x1;
        value  >>= 1;
    }

    result <<= bit_count - 1;
    bit_count <<= 1;
    bit_count -= 1;

    if (count) 
    {
        *count = bit_count;
    }
 
    return result;
}

uint32 encode_signed_golomb_value(int16 input, uint8 *count) 
{
    if (input >= -128 && input < 128) 
    {
        return encode_signed_golomb_value((int8) input, count);
    }

    uint32 result = 0;
    uint32 value = (!input ? 1 : (abs((int32) input) << 1) | ((input >> 15) & 0x1));
    uint8 bit_count = evx_required_bits(value);

    while (value) 
    {
        result <<= 1;
        result |= value & 0x1;
        value >>= 1;
    }

    result <<= bit_count - 1;
    bit_count <<= 1;
    bit_count -= 1;

    if (count) 
    {
        *count = bit_count;
    }

    return result;
}

uint16 decode_unsigned_golomb_value(uint32 input, uint8 *count) 
{
    uint16 result = 0; 
    uint8 zero_count = 0;
    uint8 bit_count = 0;

    while (!(input & 0x1)) 
    {
        zero_count++;
        input >>= 1;
    }

    bit_count = zero_count + 1;

    for (uint8 i = 0; i < bit_count; i++) 
    {
        result <<= 1;
        result |= input & 0x1;
        input  >>= 1;
    }

    result -= 1;

    if (count) 
    {
        *count = bit_count + zero_count;
    }

    return result;
}

int16 decode_signed_golomb_value(uint32 input, uint8 *count) 
{
    int16 result = 0;
    uint8 zero_count = 0;
    uint8 bit_count = 0;
    int16 sign = 0;

    while (!(input & 0x1)) 
    {
        zero_count++;
        input >>= 1;
    }

    bit_count = zero_count + 1;
    for (uint8 i = 0; i < bit_count; i++) 
    {
        result <<= 1;
        result |= input & 0x1;
        input >>= 1;
    }

    // Remove the lowest bit as our sign bit.
    sign = 1 - 2 * (result & 0x1);
    result = sign * ((result >> 1) & 0x7FFF);

    // The total number of bits consumed is (zero_count leading zeros) + (zero_count + 1 data bits)
    // = 2 * zero_count + 1, which equals bit_count + zero_count.
    uint8 total_bits = bit_count + zero_count;

    // Defend against overflow on min int16.
    if (total_bits > 0x20)
    {
        result |= 0x8000;
    }

    if (count)
    {
        *count = total_bits;
    }

    return result;
}

} // namespace evx
