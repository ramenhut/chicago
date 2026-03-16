#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include "video/evx3.h"
#include "video/image.h"
#include "bitstream.h"
#include "video/config.h"

using namespace evx;

static double compute_psnr(const uint8 *src, const uint8 *dst, int width, int height, int pitch) {
    double mse = 0;
    int count = 0;
    for (int j = 0; j < height; j++)
    for (int i = 0; i < width; i++) {
        for (int c = 0; c < 3; c++) {
            double diff = (double)src[j * pitch + i * 3 + c] - (double)dst[j * pitch + i * 3 + c];
            mse += diff * diff;
            count++;
        }
    }
    mse /= count;
    if (mse == 0) return 100.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

// Content generators

static void fill_gradient(uint8 *data, int width, int height, int pitch, int x_offset = 0) {
    for (int j = 0; j < height; j++)
    for (int i = 0; i < width; i++) {
        int idx = j * pitch + i * 3;
        data[idx + 0] = (uint8)(((i + x_offset) * 4) & 0xFF);
        data[idx + 1] = (uint8)((j * 4) & 0xFF);
        data[idx + 2] = (uint8)(((i + j + x_offset) * 2) & 0xFF);
    }
}

static void fill_sinusoidal(uint8 *data, int width, int height, int pitch) {
    for (int j = 0; j < height; j++)
    for (int i = 0; i < width; i++) {
        int idx = j * pitch + i * 3;
        data[idx + 0] = (uint8)((sin(i * 0.2) * 0.5 + 0.5) * 255);
        data[idx + 1] = (uint8)((cos(j * 0.15) * 0.5 + 0.5) * 255);
        data[idx + 2] = (uint8)((sin((i + j) * 0.1) * 0.5 + 0.5) * 255);
    }
}

static void fill_noise(uint8 *data, int width, int height, int pitch) {
    uint32 seed = 12345;
    for (int j = 0; j < height; j++)
    for (int i = 0; i < width; i++) {
        int idx = j * pitch + i * 3;
        seed = seed * 1664525u + 1013904223u; // LCG
        data[idx + 0] = (uint8)(seed & 0xFF);
        data[idx + 1] = (uint8)((seed >> 8) & 0xFF);
        data[idx + 2] = (uint8)((seed >> 16) & 0xFF);
    }
}

static void fill_flat(uint8 *data, int width, int height, int pitch) {
    for (int j = 0; j < height; j++)
    for (int i = 0; i < width; i++) {
        int idx = j * pitch + i * 3;
        data[idx + 0] = 128;
        data[idx + 1] = 64;
        data[idx + 2] = 200;
    }
}

// Helpers

struct EncodeResult {
    uint32 bytes;
    double bpp;
    double psnr;
    double encode_ms;
    double decode_ms;
};

static EncodeResult encode_decode(uint8 *src, int width, int height, int pitch,
                                  uint8 *dst, uint8 quality,
                                  evx3_encoder *enc, evx3_decoder *dec,
                                  bit_stream *stream) {
    stream->empty();

    auto t0 = std::chrono::high_resolution_clock::now();
    enc->encode(src, width, height, stream);
    auto t1 = std::chrono::high_resolution_clock::now();

    EncodeResult r;
    r.bytes = stream->query_byte_occupancy();

    dec->decode(stream, dst);
    auto t2 = std::chrono::high_resolution_clock::now();
    r.bpp = (r.bytes * 8.0) / (width * height);
    r.psnr = compute_psnr(src, dst, width, height, pitch);
    r.encode_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.decode_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    return r;
}

// --- Test 1: Compression Efficiency Table ---

TEST(BenchmarkTest, CompressionEfficiencyTable) {
    const int W = 128, H = 128;
    image src, dst;
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &src);
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &dst);
    bit_stream stream;
    stream.resize_capacity(4 * EVX_MB * 8);

    fill_gradient(src.query_data(), W, H, src.query_row_pitch());

    uint8 qualities[] = {1, 4, 8, 12, 16, 20, 24, 28};
    int raw_bytes = W * H * 3;

    printf("\n=== Compression Efficiency (128x128 Gradient) ===\n");
    printf("Quality |  Bytes |   BPP  | PSNR (dB) | Ratio\n");
    printf("--------|--------|--------|-----------|------\n");

    double first_bpp = 0, last_bpp = 0;
    double first_psnr = 0;

    for (int q = 0; q < 8; q++) {
        evx3_encoder *enc = nullptr;
        evx3_decoder *dec = nullptr;
        create_encoder(&enc);
        create_decoder(&dec);
        enc->set_quality(qualities[q]);

        auto r = encode_decode(src.query_data(), W, H, src.query_row_pitch(),
                               dst.query_data(), qualities[q], enc, dec, &stream);

        double ratio = (double)raw_bytes / r.bytes;
        printf("   %4d | %6u | %6.2f | %9.1f | %5.1fx\n",
               qualities[q], r.bytes, r.bpp, r.psnr, ratio);

        if (q == 0) { first_bpp = r.bpp; first_psnr = r.psnr; }
        if (q == 7) { last_bpp = r.bpp; }

        destroy_encoder(enc);
        destroy_decoder(dec);
    }

    EXPECT_GT(first_psnr, 20.0) << "PSNR at quality 1 should exceed 20 dB";
    EXPECT_LT(last_bpp, first_bpp) << "bpp at quality 28 should be less than at quality 1";

    destroy_image(&src);
    destroy_image(&dst);
}

// --- Test 2: Content Type Comparison ---

TEST(BenchmarkTest, ContentTypeComparison) {
    const int W = 128, H = 128;
    const uint8 QUALITY = 8;
    image src, dst;
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &src);
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &dst);
    bit_stream stream;
    stream.resize_capacity(4 * EVX_MB * 8);

    const char *names[] = {"Gradient", "Sinusoidal", "Noise", "Flat"};
    void (*fillers[])(uint8*, int, int, int) = {
        [](uint8 *d, int w, int h, int p) { fill_gradient(d, w, h, p); },
        fill_sinusoidal, fill_noise, fill_flat
    };

    printf("\n=== Content Type Comparison (128x128, quality=%d) ===\n", QUALITY);
    printf("Content    |  Bytes |   BPP  | PSNR (dB)\n");
    printf("-----------|--------|--------|----------\n");

    double flat_psnr = 0, noise_bpp = 0;
    double psnrs[4], bpps[4];

    for (int c = 0; c < 4; c++) {
        evx3_encoder *enc = nullptr;
        evx3_decoder *dec = nullptr;
        create_encoder(&enc);
        create_decoder(&dec);
        enc->set_quality(QUALITY);

        fillers[c](src.query_data(), W, H, src.query_row_pitch());

        auto r = encode_decode(src.query_data(), W, H, src.query_row_pitch(),
                               dst.query_data(), QUALITY, enc, dec, &stream);

        printf("%-10s | %6u | %6.2f | %9.1f\n", names[c], r.bytes, r.bpp, r.psnr);
        psnrs[c] = r.psnr;
        bpps[c] = r.bpp;

        destroy_encoder(enc);
        destroy_decoder(dec);
    }

    flat_psnr = psnrs[3];
    noise_bpp = bpps[2];

    // Flat should have highest PSNR
    for (int c = 0; c < 3; c++) {
        EXPECT_GE(flat_psnr, psnrs[c])
            << "Flat PSNR (" << flat_psnr << ") should be >= " << names[c] << " (" << psnrs[c] << ")";
    }
    // Noise should have highest bpp
    for (int c = 0; c < 4; c++) {
        if (c == 2) continue;
        EXPECT_GE(noise_bpp, bpps[c])
            << "Noise bpp (" << noise_bpp << ") should be >= " << names[c] << " (" << bpps[c] << ")";
    }

    destroy_image(&src);
    destroy_image(&dst);
}

// --- Test 3: Resolution Scaling ---

TEST(BenchmarkTest, ResolutionScaling) {
    const uint8 QUALITY = 8;
    int sizes[] = {64, 128, 256};

    printf("\n=== Resolution Scaling (Gradient, quality=%d) ===\n", QUALITY);
    printf("Resolution |  Bytes |   BPP  | PSNR (dB) | Enc ms | Dec ms\n");
    printf("-----------|--------|--------|-----------|--------|-------\n");

    for (int s = 0; s < 3; s++) {
        int W = sizes[s], H = sizes[s];
        image src, dst;
        create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &src);
        create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &dst);
        bit_stream stream;
        stream.resize_capacity(4 * EVX_MB * 8);

        evx3_encoder *enc = nullptr;
        evx3_decoder *dec = nullptr;
        create_encoder(&enc);
        create_decoder(&dec);
        enc->set_quality(QUALITY);

        fill_gradient(src.query_data(), W, H, src.query_row_pitch());

        auto r = encode_decode(src.query_data(), W, H, src.query_row_pitch(),
                               dst.query_data(), QUALITY, enc, dec, &stream);

        printf("  %3dx%-3d  | %6u | %6.2f | %9.1f | %6.1f | %5.1f\n",
               W, H, r.bytes, r.bpp, r.psnr, r.encode_ms, r.decode_ms);

        EXPECT_GT(r.bytes, 0u) << "Encode should produce output at " << W << "x" << H;
        EXPECT_GT(r.psnr, 15.0) << "PSNR too low at " << W << "x" << H;

        destroy_encoder(enc);
        destroy_decoder(dec);
        destroy_image(&src);
        destroy_image(&dst);
    }
}

// --- Test 4: Encode/Decode Timing ---

TEST(BenchmarkTest, EncodeDecodeTiming) {
    const int W = 128, H = 128;
    const uint8 QUALITY = 8;
    const int ITERATIONS = 10;

    image src, dst;
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &src);
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &dst);
    bit_stream stream;
    stream.resize_capacity(4 * EVX_MB * 8);

    fill_gradient(src.query_data(), W, H, src.query_row_pitch());

    double total_enc = 0, total_dec = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        evx3_encoder *enc = nullptr;
        evx3_decoder *dec = nullptr;
        create_encoder(&enc);
        create_decoder(&dec);
        enc->set_quality(QUALITY);

        auto r = encode_decode(src.query_data(), W, H, src.query_row_pitch(),
                               dst.query_data(), QUALITY, enc, dec, &stream);
        total_enc += r.encode_ms;
        total_dec += r.decode_ms;

        destroy_encoder(enc);
        destroy_decoder(dec);
    }

    double avg_enc = total_enc / ITERATIONS;
    double avg_dec = total_dec / ITERATIONS;

    printf("\n=== Encode/Decode Timing (128x128, quality=%d, %d iterations) ===\n", QUALITY, ITERATIONS);
    printf("Avg encode: %.2f ms/frame\n", avg_enc);
    printf("Avg decode: %.2f ms/frame\n", avg_dec);
    printf("Avg total:  %.2f ms/frame\n", avg_enc + avg_dec);

    EXPECT_GT(total_enc, 0.0) << "Encode should take measurable time";
    EXPECT_GT(total_dec, 0.0) << "Decode should take measurable time";

    destroy_image(&src);
    destroy_image(&dst);
}

// --- Test 5: Multi-Frame Efficiency ---

TEST(BenchmarkTest, MultiFrameEfficiency) {
    const int W = 128, H = 128;
    const uint8 QUALITY = 8;
    const int NUM_FRAMES = 10;

    image src, dst;
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &src);
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &dst);
    bit_stream stream;
    stream.resize_capacity(4 * EVX_MB * 8);

    evx3_encoder *enc = nullptr;
    evx3_decoder *dec = nullptr;
    create_encoder(&enc);
    create_decoder(&dec);
    enc->set_quality(QUALITY);

    printf("\n=== Multi-Frame Efficiency (128x128, quality=%d, %d frames) ===\n", QUALITY, NUM_FRAMES);
    printf("Frame | Type |  Bytes |   BPP  | PSNR (dB) | Enc ms\n");
    printf("------|------|--------|--------|-----------|-------\n");

    double iframe_total = 0, pframe_total = 0;
    int iframe_count = 0, pframe_count = 0;

    for (int f = 0; f < NUM_FRAMES; f++) {
        fill_gradient(src.query_data(), W, H, src.query_row_pitch(), f * 2);
        stream.empty();

        auto t0 = std::chrono::high_resolution_clock::now();
        enc->encode(src.query_data(), W, H, &stream);
        auto t1 = std::chrono::high_resolution_clock::now();

        uint32 bytes = stream.query_byte_occupancy();

        dec->decode(&stream, dst.query_data());
        double bpp = (bytes * 8.0) / (W * H);
        double psnr = compute_psnr(src.query_data(), dst.query_data(), W, H, src.query_row_pitch());
        double enc_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        const char *type = (f == 0) ? "I" : "P";
        printf("  %3d |  %s   | %6u | %6.2f | %9.1f | %5.1f\n",
               f, type, bytes, bpp, psnr, enc_ms);

        if (f == 0) {
            iframe_total += bytes;
            iframe_count++;
        } else {
            pframe_total += bytes;
            pframe_count++;
        }
    }

    double avg_i = iframe_total / iframe_count;
    double avg_p = pframe_total / pframe_count;

    printf("------\n");
    printf("Avg I-frame: %.0f bytes\n", avg_i);
    printf("Avg P-frame: %.0f bytes\n", avg_p);
    printf("P/I ratio:   %.2f\n", avg_p / avg_i);

    EXPECT_LT(avg_p, avg_i) << "P-frames should be smaller than I-frames on average";

    destroy_encoder(enc);
    destroy_decoder(dec);
    destroy_image(&src);
    destroy_image(&dst);
}

// --- Test 6: Rate-Distortion Curve ---

TEST(BenchmarkTest, RateDistortionCurve) {
    const int W = 128, H = 128;
    image src, dst;
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &src);
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &dst);
    bit_stream stream;
    stream.resize_capacity(4 * EVX_MB * 8);

    fill_sinusoidal(src.query_data(), W, H, src.query_row_pitch());

    uint8 qualities[] = {1, 2, 4, 6, 8, 10, 12, 16, 20, 24, 28};
    int num_q = sizeof(qualities) / sizeof(qualities[0]);

    printf("\n=== Rate-Distortion Curve (128x128 Sinusoidal) ===\n");
    printf("Quality |   BPP  | PSNR (dB)\n");
    printf("--------|--------|----------\n");

    double prev_psnr = 200.0;
    bool monotonic = true;

    for (int q = 0; q < num_q; q++) {
        evx3_encoder *enc = nullptr;
        evx3_decoder *dec = nullptr;
        create_encoder(&enc);
        create_decoder(&dec);
        enc->set_quality(qualities[q]);

        auto r = encode_decode(src.query_data(), W, H, src.query_row_pitch(),
                               dst.query_data(), qualities[q], enc, dec, &stream);

        printf("   %4d | %6.2f | %9.1f\n", qualities[q], r.bpp, r.psnr);

        if (q > 0 && r.psnr > prev_psnr + 3.5) {
            monotonic = false;
        }
        prev_psnr = r.psnr;

        destroy_encoder(enc);
        destroy_decoder(dec);
    }

    EXPECT_TRUE(monotonic) << "PSNR should generally decrease as quality number increases";

    destroy_image(&src);
    destroy_image(&dst);
}

#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)

TEST(BenchmarkTest, BFrameCompressionEfficiency) {
    const int W = 128, H = 128;
    const int NUM_FRAMES = 8;
    const uint8 QUALITY = 8;

    image src_img;
    create_image(EVX_IMAGE_FORMAT_R8G8B8, W, H, &src_img);
    uint32 pitch = src_img.query_row_pitch();

    // Generate frame data
    std::vector<std::vector<uint8>> frames(NUM_FRAMES);
    for (int f = 0; f < NUM_FRAMES; f++) {
        frames[f].resize(W * H * 3);
        fill_gradient(frames[f].data(), W, H, W * 3, f * 4);
    }

    // Encode with B-frames
    evx3_encoder *enc = nullptr;
    create_encoder(&enc);
    enc->set_quality(QUALITY);

    bit_stream stream;
    stream.resize_capacity(16 * EVX_MB * 8);

    auto t0 = std::chrono::high_resolution_clock::now();

    for (int f = 0; f < NUM_FRAMES; f++) {
        EXPECT_EQ(enc->encode(frames[f].data(), W, H, &stream), EVX_SUCCESS);
    }
    EXPECT_EQ(enc->flush(&stream), EVX_SUCCESS);

    auto t1 = std::chrono::high_resolution_clock::now();
    double encode_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    uint32 total_bytes = stream.query_byte_occupancy();
    double bpp = (double)total_bytes * 8.0 / (W * H * NUM_FRAMES);

    // Decode and measure PSNR
    evx3_decoder *dec = nullptr;
    create_decoder(&dec);
    std::vector<uint8> rgb_buf(W * H * 3);
    std::vector<std::vector<uint8>> decoded;

    auto t2 = std::chrono::high_resolution_clock::now();

    int dec_count = 0;
    while (stream.query_byte_occupancy() > 0) {
        if (dec->decode(&stream, rgb_buf.data()) != EVX_SUCCESS) break;
        if (dec->has_output())
            decoded.push_back(std::vector<uint8>(rgb_buf.begin(), rgb_buf.end()));
        dec_count++;
    }
    while (true) {
        dec->flush(rgb_buf.data());
        if (!dec->has_output()) break;
        decoded.push_back(std::vector<uint8>(rgb_buf.begin(), rgb_buf.end()));
    }

    auto t3 = std::chrono::high_resolution_clock::now();
    double decode_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // Compute average PSNR
    double total_psnr = 0;
    int psnr_count = 0;
    for (int f = 0; f < (int)decoded.size() && f < NUM_FRAMES; f++) {
        total_psnr += compute_psnr(frames[f].data(), decoded[f].data(), W, H, W * 3);
        psnr_count++;
    }
    double avg_psnr = (psnr_count > 0) ? total_psnr / psnr_count : 0;

    printf("\n=== B-Frame Compression Efficiency (q=%d, %dx%d, %d frames) ===\n",
           QUALITY, W, H, NUM_FRAMES);
    printf("  Total size: %u bytes (%.3f bpp)\n", total_bytes, bpp);
    printf("  Avg PSNR:   %.2f dB\n", avg_psnr);
    printf("  Encode:     %.1f ms (%.1f fps)\n", encode_ms, NUM_FRAMES * 1000.0 / encode_ms);
    printf("  Decode:     %.1f ms (%.1f fps)\n", decode_ms, NUM_FRAMES * 1000.0 / decode_ms);
    printf("  Frames decoded: %d/%d\n", (int)decoded.size(), NUM_FRAMES);

    EXPECT_EQ((int)decoded.size(), NUM_FRAMES) << "All frames should be decoded";
    EXPECT_GT(avg_psnr, 20.0) << "Average PSNR should be reasonable";

    destroy_encoder(enc);
    destroy_decoder(dec);
    destroy_image(&src_img);
}

#endif // EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
