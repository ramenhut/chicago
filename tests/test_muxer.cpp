#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "muxer.h"

using namespace evx;

static const char *TEST_MUX_PATH = "/tmp/evx_test_muxer.evx";

TEST(Muxer, ContainerHeaderRoundtrip) {
    // Write.
    {
        evx_muxer muxer;
        ASSERT_EQ(muxer.open(TEST_MUX_PATH), EVX_SUCCESS);

        uint8 vid_id = 0, aud_id = 0;
        ASSERT_EQ(muxer.add_video_stream(320, 240, 30.0f, &vid_id), EVX_SUCCESS);
        ASSERT_EQ(muxer.add_audio_stream(44100, 2, 16, 8, &aud_id), EVX_SUCCESS);
        EXPECT_EQ(vid_id, 0);
        EXPECT_EQ(aud_id, 1);

        ASSERT_EQ(muxer.write_header(), EVX_SUCCESS);
        ASSERT_EQ(muxer.close(), EVX_SUCCESS);
    }

    // Read back.
    {
        evx_demuxer demuxer;
        ASSERT_EQ(demuxer.open(TEST_MUX_PATH), EVX_SUCCESS);
        ASSERT_EQ(demuxer.read_header(), EVX_SUCCESS);

        EXPECT_EQ(demuxer.query_stream_count(), 2);

        const evx_stream_descriptor &vid = demuxer.query_stream(0);
        EXPECT_EQ(vid.stream_type, EVX_STREAM_TYPE_VIDEO);
        EXPECT_EQ(vid.width, 320);
        EXPECT_EQ(vid.height, 240);
        EXPECT_NEAR(vid.frame_rate, 30.0f, 0.01f);

        const evx_stream_descriptor &aud = demuxer.query_stream(1);
        EXPECT_EQ(aud.stream_type, EVX_STREAM_TYPE_AUDIO);
        EXPECT_EQ(aud.sample_rate, 44100u);
        EXPECT_EQ(aud.channels, 2);
        EXPECT_EQ(aud.bit_depth, 16);
        EXPECT_EQ(aud.quality, 8);

        ASSERT_EQ(demuxer.close(), EVX_SUCCESS);
    }

    remove(TEST_MUX_PATH);
}

TEST(Muxer, InterleavedPacketRouting) {
    {
        evx_muxer muxer;
        ASSERT_EQ(muxer.open(TEST_MUX_PATH), EVX_SUCCESS);

        uint8 vid_id = 0, aud_id = 0;
        muxer.add_video_stream(320, 240, 30.0f, &vid_id);
        muxer.add_audio_stream(44100, 1, 16, 8, &aud_id);
        muxer.write_header();

        // Write interleaved packets.
        uint8 vid_payload[] = {0xDE, 0xAD};
        uint8 aud_payload[] = {0xBE, 0xEF, 0xCA, 0xFE};

        ASSERT_EQ(muxer.write_packet(vid_id, EVX_STREAM_TYPE_VIDEO, 0, vid_payload, 2), EVX_SUCCESS);
        ASSERT_EQ(muxer.write_packet(aud_id, EVX_STREAM_TYPE_AUDIO, 1000, aud_payload, 4), EVX_SUCCESS);
        ASSERT_EQ(muxer.write_packet(vid_id, EVX_STREAM_TYPE_VIDEO, 33333, vid_payload, 2), EVX_SUCCESS);
        ASSERT_EQ(muxer.write_packet(aud_id, EVX_STREAM_TYPE_AUDIO, 34000, aud_payload, 4), EVX_SUCCESS);

        muxer.close();
    }

    // Read and verify routing.
    {
        evx_demuxer demuxer;
        ASSERT_EQ(demuxer.open(TEST_MUX_PATH), EVX_SUCCESS);
        ASSERT_EQ(demuxer.read_header(), EVX_SUCCESS);

        evx_demux_packet pkt;

        // Packet 1: video.
        ASSERT_EQ(demuxer.read_packet(&pkt), EVX_SUCCESS);
        EXPECT_EQ(pkt.stream_type, EVX_STREAM_TYPE_VIDEO);
        EXPECT_EQ(pkt.stream_id, 0);
        EXPECT_EQ(pkt.timestamp, 0u);
        EXPECT_EQ(pkt.payload.size(), 2u);
        EXPECT_EQ(pkt.payload[0], 0xDE);

        // Packet 2: audio.
        ASSERT_EQ(demuxer.read_packet(&pkt), EVX_SUCCESS);
        EXPECT_EQ(pkt.stream_type, EVX_STREAM_TYPE_AUDIO);
        EXPECT_EQ(pkt.stream_id, 1);
        EXPECT_EQ(pkt.timestamp, 1000u);
        EXPECT_EQ(pkt.payload.size(), 4u);

        // Packet 3: video.
        ASSERT_EQ(demuxer.read_packet(&pkt), EVX_SUCCESS);
        EXPECT_EQ(pkt.stream_type, EVX_STREAM_TYPE_VIDEO);
        EXPECT_EQ(pkt.timestamp, 33333u);

        // Packet 4: audio.
        ASSERT_EQ(demuxer.read_packet(&pkt), EVX_SUCCESS);
        EXPECT_EQ(pkt.stream_type, EVX_STREAM_TYPE_AUDIO);
        EXPECT_EQ(pkt.timestamp, 34000u);

        // EOF.
        EXPECT_EQ(demuxer.read_packet(&pkt), EVX_ERROR_OPERATION_COMPLETED);

        demuxer.close();
    }

    remove(TEST_MUX_PATH);
}

TEST(Muxer, TimestampPreservation) {
    {
        evx_muxer muxer;
        ASSERT_EQ(muxer.open(TEST_MUX_PATH), EVX_SUCCESS);

        uint8 sid = 0;
        muxer.add_audio_stream(48000, 2, 16, 4, &sid);
        muxer.write_header();

        // Write packets with specific timestamps.
        uint8 data[] = {0};
        uint64 timestamps[] = {0, 10667, 21333, 32000, 42667};

        for (int i = 0; i < 5; i++) {
            muxer.write_packet(sid, EVX_STREAM_TYPE_AUDIO, timestamps[i], data, 1);
        }

        muxer.close();
    }

    {
        evx_demuxer demuxer;
        ASSERT_EQ(demuxer.open(TEST_MUX_PATH), EVX_SUCCESS);
        ASSERT_EQ(demuxer.read_header(), EVX_SUCCESS);

        uint64 expected[] = {0, 10667, 21333, 32000, 42667};
        evx_demux_packet pkt;

        for (int i = 0; i < 5; i++) {
            ASSERT_EQ(demuxer.read_packet(&pkt), EVX_SUCCESS);
            EXPECT_EQ(pkt.timestamp, expected[i]) << "Timestamp mismatch at packet " << i;
        }

        demuxer.close();
    }

    remove(TEST_MUX_PATH);
}
