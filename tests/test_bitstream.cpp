#include <gtest/gtest.h>
#include "bitstream.h"

using namespace evx;

TEST(BitStream, DefaultConstruction) {
    bit_stream bs;
    EXPECT_EQ(bs.query_occupancy(), 0u);
}

TEST(BitStream, WriteBitReadBit) {
    bit_stream bs;
    bs.resize_capacity(64);

    EXPECT_EQ(bs.write_bit(1), EVX_SUCCESS);
    EXPECT_EQ(bs.write_bit(0), EVX_SUCCESS);
    EXPECT_EQ(bs.write_bit(1), EVX_SUCCESS);
    EXPECT_EQ(bs.query_occupancy(), 3u);

    uint8 bit = 0;
    EXPECT_EQ(bs.read_bit(&bit), EVX_SUCCESS);
    EXPECT_EQ(bit, 1u);
    EXPECT_EQ(bs.read_bit(&bit), EVX_SUCCESS);
    EXPECT_EQ(bit, 0u);
    EXPECT_EQ(bs.read_bit(&bit), EVX_SUCCESS);
    EXPECT_EQ(bit, 1u);
}

TEST(BitStream, WriteByteReadByte) {
    bit_stream bs;
    bs.resize_capacity(256);

    EXPECT_EQ(bs.write_byte(0xAB), EVX_SUCCESS);
    EXPECT_EQ(bs.write_byte(0xCD), EVX_SUCCESS);

    uint8 val = 0;
    EXPECT_EQ(bs.read_byte(&val), EVX_SUCCESS);
    EXPECT_EQ(val, 0xAB);
    EXPECT_EQ(bs.read_byte(&val), EVX_SUCCESS);
    EXPECT_EQ(val, 0xCD);
}

TEST(BitStream, WriteBitsReadBits) {
    bit_stream bs;
    bs.resize_capacity(256);

    uint32 val_in = 0x1F; // 5 bits worth
    EXPECT_EQ(bs.write_bits(&val_in, 5), EVX_SUCCESS);

    uint32 val_out = 0;
    EXPECT_EQ(bs.read_bits(&val_out, 5), EVX_SUCCESS);
    EXPECT_EQ(val_out, 0x1Fu);
}

TEST(BitStream, Seek) {
    bit_stream bs;
    bs.resize_capacity(256);

    bs.write_byte(0x42);
    bs.write_byte(0x43);

    uint8 val = 0;
    bs.read_byte(&val);
    EXPECT_EQ(val, 0x42);

    // Seek back
    bs.seek(-8);
    bs.read_byte(&val);
    EXPECT_EQ(val, 0x42);
}

TEST(BitStream, EmptyClear) {
    bit_stream bs;
    bs.resize_capacity(256);

    bs.write_byte(0xFF);
    EXPECT_GT(bs.query_occupancy(), 0u);

    bs.empty();
    EXPECT_EQ(bs.query_occupancy(), 0u);
}

TEST(BitStream, Capacity) {
    bit_stream bs;
    bs.resize_capacity(128);
    EXPECT_GE(bs.query_capacity(), 128u);
}

TEST(BitStream, PeekDoesNotConsume) {
    bit_stream bs;
    bs.resize_capacity(256);

    bs.write_byte(0xAB);

    uint8 peek_val = 0;
    EXPECT_EQ(bs.peek_byte(&peek_val), EVX_SUCCESS);
    EXPECT_EQ(peek_val, 0xAB);

    // Read position should not have advanced
    uint8 read_val = 0;
    EXPECT_EQ(bs.read_byte(&read_val), EVX_SUCCESS);
    EXPECT_EQ(read_val, 0xAB);
}

TEST(BitStream, MultipleBytesRoundtrip) {
    bit_stream bs;
    bs.resize_capacity(1024);

    uint8 data[] = {0x00, 0xFF, 0x55, 0xAA, 0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(bs.write_bytes(data, sizeof(data)), EVX_SUCCESS);

    uint8 result[8] = {};
    EXPECT_EQ(bs.read_bytes(result, sizeof(result)), EVX_SUCCESS);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(result[i], data[i]) << "Mismatch at byte " << i;
    }
}
