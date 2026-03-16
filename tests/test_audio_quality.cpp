#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "audio/audio.h"
#include "audio/config.h"
#include "audio/types.h"
#include "bitstream.h"

using namespace evx;

static const float PI = 3.14159265358979323846f;

namespace evx {
void audio_decoder_set_header(evx3_audio_decoder *decoder, const evx_audio_header &header);
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

struct QualityResult {
    uint8 quality;
    double snr;
    uint32 bitrate_bytes;
};

static QualityResult run_quality_test(uint8 quality) {
    const uint32 sample_rate = 44100;
    const uint8 channels = 1;
    const uint32 hop = EVX_AUDIO_HOP_SIZE;
    const int num_frames = 20;

    evx3_audio_encoder *encoder = nullptr;
    evx3_audio_decoder *decoder = nullptr;
    create_audio_encoder(sample_rate, channels, &encoder);
    create_audio_decoder(&decoder);

    evx_audio_header hdr;
    initialize_audio_header(sample_rate, channels, &hdr);
    audio_decoder_set_header(decoder, hdr);

    encoder->set_quality(quality);

    bit_stream coded;
    coded.resize_capacity(1 * EVX_MB * 8);

    // Generate a complex test signal (sum of sines).
    std::vector<float32> input(hop * num_frames);
    std::vector<float32> output(hop * num_frames);

    for (uint32 i = 0; i < hop * num_frames; i++) {
        float32 t = (float32)i / sample_rate;
        input[i] = 0.3f * sinf(2.0f * PI * 440.0f * t) +
                   0.2f * sinf(2.0f * PI * 880.0f * t) +
                   0.1f * sinf(2.0f * PI * 1760.0f * t);
    }

    uint32 total_coded_bytes = 0;

    for (int f = 0; f < num_frames; f++) {
        coded.empty();
        encoder->encode(input.data() + f * hop, hop, &coded);
        total_coded_bytes += coded.query_byte_occupancy();

        uint32 out_count = 0;
        decoder->decode(&coded, output.data() + f * hop, &out_count);
    }

    // Skip priming frames. MDCT overlap-add introduces a one-hop delay:
    // output at frame f reconstructs input from frame f-1.
    uint32 out_skip = hop * 2;
    uint32 in_skip = hop;
    uint32 test_count = hop * (num_frames - 2);
    double snr = compute_snr(input.data() + in_skip, output.data() + out_skip, test_count);

    destroy_audio_encoder(encoder);
    destroy_audio_decoder(decoder);

    return {quality, snr, total_coded_bytes};
}

TEST(AudioQuality, SnrMonotonicallyDecreases) {
    // Test qualities: 0 (best), 4, 8, 16, 24
    std::vector<uint8> qualities = {0, 4, 8, 16, 24};
    std::vector<QualityResult> results;

    for (uint8 q : qualities) {
        results.push_back(run_quality_test(q));
    }

    // SNR should monotonically decrease as quality number increases.
    for (size_t i = 1; i < results.size(); i++) {
        EXPECT_GE(results[i - 1].snr, results[i].snr)
            << "SNR should decrease: q=" << (int)results[i - 1].quality
            << " (" << results[i - 1].snr << " dB) vs q="
            << (int)results[i].quality << " (" << results[i].snr << " dB)";
    }

    // Best quality should have good SNR.
    EXPECT_GT(results[0].snr, 30.0)
        << "Quality=0 should achieve at least 30 dB SNR";
}

TEST(AudioQuality, BitrateMonotonicallyDecreases) {
    std::vector<uint8> qualities = {0, 4, 8, 16, 24};
    std::vector<QualityResult> results;

    for (uint8 q : qualities) {
        results.push_back(run_quality_test(q));
    }

    // Bitrate should monotonically decrease as quality number increases.
    for (size_t i = 1; i < results.size(); i++) {
        EXPECT_GE(results[i - 1].bitrate_bytes, results[i].bitrate_bytes)
            << "Bitrate should decrease: q=" << (int)results[i - 1].quality
            << " (" << results[i - 1].bitrate_bytes << " B) vs q="
            << (int)results[i].quality << " (" << results[i].bitrate_bytes << " B)";
    }
}
