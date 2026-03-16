#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <vector>
#include "audio/audio.h"
#include "audio/config.h"
#include "audio/types.h"
#include "audio/serialize.h"
#include "bitstream.h"
#include "abac.h"

using namespace evx;

static const float PI = 3.14159265358979323846f;

static double compute_snr(const float32 *reference, const float32 *test,
                          uint32 count) {
    double signal_power = 0, noise_power = 0;
    for (uint32 i = 0; i < count; i++) {
        signal_power += (double)reference[i] * reference[i];
        double err = (double)reference[i] - test[i];
        noise_power += err * err;
    }
    if (noise_power == 0) return 100.0;
    return 10.0 * log10(signal_power / noise_power);
}

namespace evx {
void audio_decoder_set_header(evx3_audio_decoder *decoder, const evx_audio_header &header);
}

class AudioCodecTest : public ::testing::Test {
protected:
    evx3_audio_encoder *encoder = nullptr;
    evx3_audio_decoder *decoder = nullptr;
    bit_stream coded_stream;

    static constexpr uint32 SAMPLE_RATE = 44100;
    static constexpr uint8 CHANNELS = 1;
    static constexpr uint32 HOP = EVX_AUDIO_HOP_SIZE;

    void SetUp() override {
        create_audio_encoder(SAMPLE_RATE, CHANNELS, &encoder);
        create_audio_decoder(&decoder);

        evx_audio_header hdr;
        initialize_audio_header(SAMPLE_RATE, CHANNELS, &hdr);
        audio_decoder_set_header(decoder, hdr);

        coded_stream.resize_capacity(1 * EVX_MB * 8);
    }

    void TearDown() override {
        destroy_audio_encoder(encoder);
        destroy_audio_decoder(decoder);
    }
};

TEST_F(AudioCodecTest, SilenceRoundtrip) {
    encoder->set_quality(0);

    std::vector<float32> silence(HOP, 0.0f);
    std::vector<float32> output(HOP, 0.0f);
    uint32 out_count = 0;

    // Encode 3 frames (first is priming).
    for (int f = 0; f < 3; f++) {
        coded_stream.empty();
        ASSERT_EQ(encoder->encode(silence.data(), HOP, &coded_stream), EVX_SUCCESS);

        ASSERT_EQ(decoder->decode(&coded_stream, output.data(), &out_count), EVX_SUCCESS);
        EXPECT_EQ(out_count, HOP);
    }

    // After priming, output should be near-silent.
    for (uint32 i = 0; i < HOP; i++) {
        EXPECT_NEAR(output[i], 0.0f, 0.01f) << "Silence output not near zero at " << i;
    }
}

TEST_F(AudioCodecTest, SineWaveRoundtrip) {
    encoder->set_quality(0);

    const int num_frames = 10;
    std::vector<float32> input_pcm(HOP * num_frames);
    std::vector<float32> output_pcm(HOP * num_frames);

    // Generate 440 Hz sine wave.
    for (uint32 i = 0; i < HOP * num_frames; i++) {
        input_pcm[i] = 0.5f * sinf(2.0f * PI * 440.0f * i / SAMPLE_RATE);
    }

    // Encode and decode all frames.
    for (int f = 0; f < num_frames; f++) {
        coded_stream.empty();
        ASSERT_EQ(encoder->encode(input_pcm.data() + f * HOP, HOP, &coded_stream), EVX_SUCCESS);

        uint32 out_count = 0;
        ASSERT_EQ(decoder->decode(&coded_stream, output_pcm.data() + f * HOP, &out_count), EVX_SUCCESS);
        EXPECT_EQ(out_count, HOP);
    }

    // MDCT overlap-add introduces a one-hop delay: frame f's output
    // reconstructs input from frame f-1.  Skip 2 frames for priming,
    // then compare output[2*HOP..] against input[HOP..].
    uint32 out_skip = HOP * 2;
    uint32 in_skip = HOP;
    uint32 test_samples = HOP * (num_frames - 2);
    double snr = compute_snr(input_pcm.data() + in_skip,
                             output_pcm.data() + out_skip, test_samples);

    EXPECT_GT(snr, 40.0) << "Sine wave SNR at quality=0 should exceed 40 dB, got " << snr;
}

TEST_F(AudioCodecTest, MultiFrameContinuity) {
    encoder->set_quality(4);

    const int num_frames = 20;
    std::vector<float32> output_pcm(HOP * num_frames, 0.0f);

    // Encode a continuous chirp signal.
    for (int f = 0; f < num_frames; f++) {
        std::vector<float32> frame(HOP);
        for (uint32 i = 0; i < HOP; i++) {
            uint32 global_sample = f * HOP + i;
            float32 freq = 200.0f + 2000.0f * global_sample / (HOP * num_frames);
            frame[i] = 0.3f * sinf(2.0f * PI * freq * global_sample / SAMPLE_RATE);
        }

        coded_stream.empty();
        ASSERT_EQ(encoder->encode(frame.data(), HOP, &coded_stream), EVX_SUCCESS);

        uint32 out_count = 0;
        ASSERT_EQ(decoder->decode(&coded_stream, output_pcm.data() + f * HOP, &out_count), EVX_SUCCESS);
    }

    // Check for discontinuities (large jumps between frames).
    for (int f = 3; f < num_frames; f++) {
        float32 last_sample = output_pcm[f * HOP - 1];
        float32 first_sample = output_pcm[f * HOP];
        float32 jump = fabsf(first_sample - last_sample);

        // Allow larger jumps for the chirp, but no pops.
        EXPECT_LT(jump, 0.5f) << "Discontinuity at frame boundary " << f;
    }
}

class AudioCodecStereoTest : public ::testing::Test {
protected:
    evx3_audio_encoder *encoder = nullptr;
    evx3_audio_decoder *decoder = nullptr;
    bit_stream coded_stream;

    static constexpr uint32 SAMPLE_RATE = 44100;
    static constexpr uint8 CHANNELS = 2;
    static constexpr uint32 HOP = EVX_AUDIO_HOP_SIZE;

    void SetUp() override {
        create_audio_encoder(SAMPLE_RATE, CHANNELS, &encoder);
        create_audio_decoder(&decoder);

        evx_audio_header hdr;
        initialize_audio_header(SAMPLE_RATE, CHANNELS, &hdr);
        audio_decoder_set_header(decoder, hdr);

        coded_stream.resize_capacity(1 * EVX_MB * 8);
    }

    void TearDown() override {
        destroy_audio_encoder(encoder);
        destroy_audio_decoder(decoder);
    }
};

TEST_F(AudioCodecStereoTest, StereoSeparation) {
    encoder->set_quality(0);

    const int num_frames = 10;

    // Left channel: 440 Hz, Right channel: 880 Hz.
    std::vector<float32> input(HOP * CHANNELS * num_frames);
    std::vector<float32> output(HOP * CHANNELS * num_frames);

    for (int f = 0; f < num_frames; f++) {
        for (uint32 i = 0; i < HOP; i++) {
            uint32 global = f * HOP + i;
            float32 left = 0.5f * sinf(2.0f * PI * 440.0f * global / SAMPLE_RATE);
            float32 right = 0.5f * sinf(2.0f * PI * 880.0f * global / SAMPLE_RATE);
            input[(f * HOP + i) * CHANNELS + 0] = left;
            input[(f * HOP + i) * CHANNELS + 1] = right;
        }
    }

    for (int f = 0; f < num_frames; f++) {
        coded_stream.empty();
        ASSERT_EQ(encoder->encode(input.data() + f * HOP * CHANNELS, HOP, &coded_stream), EVX_SUCCESS);

        uint32 out_count = 0;
        ASSERT_EQ(decoder->decode(&coded_stream, output.data() + f * HOP * CHANNELS, &out_count), EVX_SUCCESS);
    }

    // Account for one-hop MDCT delay: output at frame f reconstructs frame f-1.
    uint32 out_skip = HOP * 2;
    uint32 in_skip = HOP;
    uint32 test_samples = HOP * (num_frames - 2);

    std::vector<float32> in_l(test_samples), in_r(test_samples);
    std::vector<float32> out_l(test_samples), out_r(test_samples);

    for (uint32 i = 0; i < test_samples; i++) {
        in_l[i] = input[(in_skip + i) * CHANNELS + 0];
        in_r[i] = input[(in_skip + i) * CHANNELS + 1];
        out_l[i] = output[(out_skip + i) * CHANNELS + 0];
        out_r[i] = output[(out_skip + i) * CHANNELS + 1];
    }

    double snr_l = compute_snr(in_l.data(), out_l.data(), test_samples);
    double snr_r = compute_snr(in_r.data(), out_r.data(), test_samples);

    EXPECT_GT(snr_l, 30.0) << "Left channel SNR should exceed 30 dB";
    EXPECT_GT(snr_r, 30.0) << "Right channel SNR should exceed 30 dB";
}

// Verify serialization roundtrip preserves quantized coefficients.
TEST(AudioSerialize, CoefficientRoundtrip) {
    int16 coeffs[EVX_AUDIO_FREQ_COEFFS];
    int16 decoded[EVX_AUDIO_FREQ_COEFFS];

    for (int k = 0; k < EVX_AUDIO_FREQ_COEFFS; k++)
        coeffs[k] = (int16)(10.0 * sin(2.0 * PI * k / 50.0));

    entropy_coder enc_coder;
    bit_stream feed, coded;
    feed.resize_capacity(64 * 1024 * 8);
    coded.resize_capacity(64 * 1024 * 8);

    audio_entropy_contexts enc_ctx, dec_ctx;
    enc_ctx.clear();
    dec_ctx.clear();

    evx_status s = audio_serialize_coefficients(coeffs, 0, &feed, &enc_coder, &enc_ctx, &coded);
    ASSERT_EQ(s, EVX_SUCCESS);
    enc_coder.finish_encode(&coded);

    entropy_coder dec_coder;
    dec_coder.start_decode(&coded);
    s = audio_unserialize_coefficients(&coded, &dec_coder, &dec_ctx, &feed, 0, decoded);
    ASSERT_EQ(s, EVX_SUCCESS);

    for (int k = 0; k < EVX_AUDIO_FREQ_COEFFS; k++) {
        EXPECT_EQ(coeffs[k], decoded[k]) << "Mismatch at k=" << k;
    }
}
