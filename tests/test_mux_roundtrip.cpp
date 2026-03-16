#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "video/evx3.h"
#include "audio/audio.h"
#include "audio/config.h"
#include "audio/types.h"
#include "muxer.h"
#include "video/image.h"
#include "bitstream.h"

using namespace evx;

static const char *TEST_MUX_ROUNDTRIP_PATH = "/tmp/evx_test_mux_roundtrip.evx";
static const float PI = 3.14159265358979323846f;

namespace evx {
void audio_decoder_set_header(evx3_audio_decoder *decoder, const evx_audio_header &header);
}

static double compute_psnr(const uint8 *src, const uint8 *dst, int count) {
    double mse = 0;
    for (int i = 0; i < count; i++) {
        double diff = (double)src[i] - (double)dst[i];
        mse += diff * diff;
    }
    mse /= count;
    if (mse == 0) return 100.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

static double compute_snr(const float32 *ref, const float32 *test, uint32 count) {
    double sig = 0, noise = 0;
    for (uint32 i = 0; i < count; i++) {
        sig += (double)ref[i] * ref[i];
        double err = (double)ref[i] - test[i];
        noise += err * err;
    }
    if (noise == 0) return 100.0;
    return 10.0 * log10(sig / noise);
}

TEST(MuxRoundtrip, CombinedAudioVideo) {
    const int WIDTH = 64;
    const int HEIGHT = 64;
    const float FPS = 30.0f;
    const uint32 SAMPLE_RATE = 44100;
    const uint8 AUDIO_CHANNELS = 1;
    const int VIDEO_FRAMES = 5;
    const uint32 HOP = EVX_AUDIO_HOP_SIZE;

    // Compute how many audio frames per video frame.
    // At 30fps, each video frame = 33333 us.
    // At 44100 Hz with 512 hop, each audio frame = 512/44100 * 1e6 = 11610 us.
    // So ~3 audio frames per video frame.
    const int AUDIO_FRAMES_PER_VIDEO = 3;
    const int TOTAL_AUDIO_FRAMES = VIDEO_FRAMES * AUDIO_FRAMES_PER_VIDEO;

    // Generate test data.
    // Video: gradient frames.
    std::vector<std::vector<uint8>> video_frames(VIDEO_FRAMES);
    for (int f = 0; f < VIDEO_FRAMES; f++) {
        video_frames[f].resize(WIDTH * HEIGHT * 3);
        for (int j = 0; j < HEIGHT; j++)
        for (int i = 0; i < WIDTH; i++) {
            int offset = (j * WIDTH + i) * 3;
            video_frames[f][offset + 0] = ((i + f * 4) * 4) & 0xFF;
            video_frames[f][offset + 1] = ((j + f * 4) * 4) & 0xFF;
            video_frames[f][offset + 2] = ((i + j) * 2) & 0xFF;
        }
    }

    // Audio: sine wave.
    std::vector<std::vector<float32>> audio_frames(TOTAL_AUDIO_FRAMES);
    for (int f = 0; f < TOTAL_AUDIO_FRAMES; f++) {
        audio_frames[f].resize(HOP);
        for (uint32 i = 0; i < HOP; i++) {
            uint32 global = f * HOP + i;
            audio_frames[f][i] = 0.5f * sinf(2.0f * PI * 440.0f * global / SAMPLE_RATE);
        }
    }

    // === ENCODE ===
    evx3_encoder *vid_enc = nullptr;
    evx3_audio_encoder *aud_enc = nullptr;
    create_encoder(&vid_enc);
    create_audio_encoder(SAMPLE_RATE, AUDIO_CHANNELS, &aud_enc);
    vid_enc->set_quality(0);
    aud_enc->set_quality(0);

    evx_muxer muxer;
    ASSERT_EQ(muxer.open(TEST_MUX_ROUNDTRIP_PATH), EVX_SUCCESS);

    uint8 vid_stream_id = 0, aud_stream_id = 0;
    muxer.add_video_stream(WIDTH, HEIGHT, FPS, &vid_stream_id);
    muxer.add_audio_stream(SAMPLE_RATE, AUDIO_CHANNELS, 16, 0, &aud_stream_id);
    muxer.write_header();

    bit_stream coded;
    coded.resize_capacity(4 * EVX_MB * 8);

    // Accumulate all video frames into a single bit_stream, then write as one packet.
    // With B-frames, the encoder buffers input frames and emits them in decode order.
    // We encode all frames first, flush, then write the complete video stream.
    bit_stream vid_coded;
    vid_coded.resize_capacity(4 * EVX_MB * 8);

    int audio_frame_idx = 0;
    for (int vf = 0; vf < VIDEO_FRAMES; vf++) {
        // Encode video frame (may produce no output if buffering B-frames).
        vid_enc->encode(video_frames[vf].data(), WIDTH, HEIGHT, &vid_coded);

        // Encode audio frames for this video frame.
        for (int af = 0; af < AUDIO_FRAMES_PER_VIDEO && audio_frame_idx < TOTAL_AUDIO_FRAMES; af++) {
            coded.empty();
            aud_enc->encode(audio_frames[audio_frame_idx].data(), HOP, &coded);
            uint64 aud_ts = (uint64)(audio_frame_idx * HOP * 1000000.0 / SAMPLE_RATE);
            muxer.write_packet(aud_stream_id, EVX_STREAM_TYPE_AUDIO, aud_ts,
                               coded.query_data(), coded.query_byte_occupancy());
            audio_frame_idx++;
        }
    }

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
    // Flush any remaining buffered B-frames.
    vid_enc->flush(&vid_coded);
#endif

    // Write the complete video stream as a single packet.
    if (vid_coded.query_byte_occupancy() > 0) {
        uint64 vid_ts = 0;
        muxer.write_packet(vid_stream_id, EVX_STREAM_TYPE_VIDEO, vid_ts,
                           vid_coded.query_data(), vid_coded.query_byte_occupancy());
    }

    muxer.close();
    destroy_encoder(vid_enc);
    destroy_audio_encoder(aud_enc);

    // === DECODE ===
    evx3_decoder *vid_dec = nullptr;
    evx3_audio_decoder *aud_dec = nullptr;
    create_decoder(&vid_dec);
    create_audio_decoder(&aud_dec);

    evx_audio_header ahdr;
    initialize_audio_header(SAMPLE_RATE, AUDIO_CHANNELS, &ahdr);
    audio_decoder_set_header(aud_dec, ahdr);

    evx_demuxer demuxer;
    ASSERT_EQ(demuxer.open(TEST_MUX_ROUNDTRIP_PATH), EVX_SUCCESS);
    ASSERT_EQ(demuxer.read_header(), EVX_SUCCESS);

    std::vector<std::vector<uint8>> decoded_video;
    std::vector<std::vector<float32>> decoded_audio;

    evx_demux_packet pkt;
    while (demuxer.read_packet(&pkt) == EVX_SUCCESS) {
        if (pkt.stream_type == EVX_STREAM_TYPE_VIDEO) {
            coded.empty();
            coded.write_bytes(pkt.payload.data(), (uint32)pkt.payload.size());

            // Decode all frames from this video packet (may contain multiple B-frames).
            while (coded.query_byte_occupancy() > 0) {
                std::vector<uint8> rgb_out(WIDTH * HEIGHT * 3);
                evx_status dec_st = vid_dec->decode(&coded, rgb_out.data());
                if (dec_st != EVX_SUCCESS) break;
                decoded_video.push_back(rgb_out);
            }
        } else if (pkt.stream_type == EVX_STREAM_TYPE_AUDIO) {
            coded.empty();
            coded.write_bytes(pkt.payload.data(), (uint32)pkt.payload.size());

            std::vector<float32> pcm_out(HOP);
            uint32 out_count = 0;
            ASSERT_EQ(aud_dec->decode(&coded, pcm_out.data(), &out_count), EVX_SUCCESS);
            decoded_audio.push_back(pcm_out);
        }
    }

    demuxer.close();
    destroy_decoder(vid_dec);
    destroy_audio_decoder(aud_dec);

    // === VERIFY ===
    EXPECT_EQ(decoded_video.size(), (size_t)VIDEO_FRAMES);
    EXPECT_EQ(decoded_audio.size(), (size_t)TOTAL_AUDIO_FRAMES);

    // Video PSNR check.
    if (!decoded_video.empty()) {
        double psnr = compute_psnr(video_frames[0].data(), decoded_video[0].data(),
                                    WIDTH * HEIGHT * 3);
        EXPECT_GT(psnr, 25.0) << "Video PSNR should exceed 25 dB";
    }

    // Audio SNR check (skip priming frames).
    // MDCT overlap-add introduces a one-hop delay: decoded frame f
    // reconstructs input from frame f-1.
    if (decoded_audio.size() > 3) {
        int skip_frames = 2;

        std::vector<float32> ref_flat, dec_flat;
        for (int f = skip_frames; f < (int)decoded_audio.size(); f++) {
            ref_flat.insert(ref_flat.end(), audio_frames[f - 1].begin(), audio_frames[f - 1].end());
            dec_flat.insert(dec_flat.end(), decoded_audio[f].begin(), decoded_audio[f].end());
        }

        double snr = compute_snr(ref_flat.data(), dec_flat.data(), (uint32)ref_flat.size());
        EXPECT_GT(snr, 30.0) << "Audio SNR should exceed 30 dB";
    }

    remove(TEST_MUX_ROUNDTRIP_PATH);
}
