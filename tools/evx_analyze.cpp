#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "evx_ffmpeg_util.h"
#include "video/evx3.h"
#include "video/config.h"
#include "muxer.h"
#include "bitstream.h"

using namespace evx;
using namespace evx_tools;

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s --source <video> --encoded <file.evx> [options]\n", prog);
    fprintf(stderr, "  -o, --output FILE    CSV output file (default: stdout)\n");
    fprintf(stderr, "  --metrics M          Comma-separated: psnr,ssim (default: psnr,ssim)\n");
}

static double compute_psnr(const uint8_t* src, const uint8_t* dst,
                           int width, int height) {
    double mse = 0;
    int count = width * height * 3;
    for (int i = 0; i < count; i++) {
        double diff = (double)src[i] - (double)dst[i];
        mse += diff * diff;
    }
    mse /= count;
    if (mse == 0.0) return 100.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Extract luminance from RGB (BT.601).
static void rgb_to_y(const uint8_t* rgb, std::vector<double>& y, int width, int height) {
    y.resize(width * height);
    for (int i = 0; i < width * height; i++) {
        y[i] = 0.299 * rgb[i * 3] + 0.587 * rgb[i * 3 + 1] + 0.114 * rgb[i * 3 + 2];
    }
}

// Compute SSIM on luminance using 8x8 block means (simplified Wang 2004).
static double compute_ssim(const uint8_t* src_rgb, const uint8_t* dst_rgb,
                           int width, int height) {
    static const double C1 = 6.5025;   // (0.01 * 255)^2
    static const double C2 = 58.5225;  // (0.03 * 255)^2
    static const int BLOCK = 8;

    std::vector<double> src_y, dst_y;
    rgb_to_y(src_rgb, src_y, width, height);
    rgb_to_y(dst_rgb, dst_y, width, height);

    double ssim_sum = 0;
    int block_count = 0;

    for (int by = 0; by + BLOCK <= height; by += BLOCK) {
        for (int bx = 0; bx + BLOCK <= width; bx += BLOCK) {
            double mean_x = 0, mean_y = 0;
            for (int j = 0; j < BLOCK; j++) {
                for (int i = 0; i < BLOCK; i++) {
                    int idx = (by + j) * width + (bx + i);
                    mean_x += src_y[idx];
                    mean_y += dst_y[idx];
                }
            }
            mean_x /= (BLOCK * BLOCK);
            mean_y /= (BLOCK * BLOCK);

            double var_x = 0, var_y = 0, cov_xy = 0;
            for (int j = 0; j < BLOCK; j++) {
                for (int i = 0; i < BLOCK; i++) {
                    int idx = (by + j) * width + (bx + i);
                    double dx = src_y[idx] - mean_x;
                    double dy = dst_y[idx] - mean_y;
                    var_x += dx * dx;
                    var_y += dy * dy;
                    cov_xy += dx * dy;
                }
            }
            var_x /= (BLOCK * BLOCK - 1);
            var_y /= (BLOCK * BLOCK - 1);
            cov_xy /= (BLOCK * BLOCK - 1);

            double num = (2.0 * mean_x * mean_y + C1) * (2.0 * cov_xy + C2);
            double den = (mean_x * mean_x + mean_y * mean_y + C1) *
                         (var_x + var_y + C2);
            ssim_sum += num / den;
            block_count++;
        }
    }

    return (block_count > 0) ? ssim_sum / block_count : 0.0;
}

int main(int argc, char** argv) {
    std::string source_path, encoded_path, output_path;
    bool do_psnr = true, do_ssim = true;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--source" && i + 1 < argc) {
            source_path = argv[++i];
        } else if (arg == "--encoded" && i + 1 < argc) {
            encoded_path = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--metrics" && i + 1 < argc) {
            std::string metrics = argv[++i];
            do_psnr = metrics.find("psnr") != std::string::npos;
            do_ssim = metrics.find("ssim") != std::string::npos;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (source_path.empty() || encoded_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    // Open source video.
    VideoReader reader;
    std::string err = reader.open(source_path);
    if (!err.empty()) {
        fprintf(stderr, "Error opening source: %s\n", err.c_str());
        return 1;
    }

    // Open .evx container.
    evx_demuxer demuxer;
    if (evx_failed(demuxer.open(encoded_path.c_str()))) {
        fprintf(stderr, "Error: Cannot open '%s'\n", encoded_path.c_str());
        return 1;
    }

    if (evx_failed(demuxer.read_header())) {
        fprintf(stderr, "Error: Invalid container header\n");
        demuxer.close();
        return 1;
    }

    // Find the video stream.
    int evx_width = 0, evx_height = 0;
    double evx_fps = 30.0;

    for (uint8 i = 0; i < demuxer.query_stream_count(); i++) {
        const evx_stream_descriptor& sd = demuxer.query_stream(i);
        if (sd.stream_type == EVX_STREAM_TYPE_VIDEO) {
            evx_width = sd.width;
            evx_height = sd.height;
            evx_fps = sd.frame_rate;
            break;
        }
    }

    if (evx_width == 0 || evx_height == 0) {
        fprintf(stderr, "Error: No video stream found in container\n");
        demuxer.close();
        return 1;
    }

    // Reopen source at EVX dimensions if they differ.
    if (reader.info().width != evx_width || reader.info().height != evx_height) {
        fprintf(stderr, "Note: Resizing source from %dx%d to %dx%d to match EVX\n",
                reader.info().width, reader.info().height, evx_width, evx_height);
        reader.close();
        err = reader.open(source_path, evx_width, evx_height);
        if (!err.empty()) {
            fprintf(stderr, "Error: %s\n", err.c_str());
            demuxer.close();
            return 1;
        }
    }

    // Create decoder.
    evx3_decoder* decoder = nullptr;
    if (evx_failed(create_decoder(&decoder))) {
        fprintf(stderr, "Error: Failed to create decoder\n");
        demuxer.close();
        return 1;
    }

    bit_stream coded;
    coded.resize_capacity(4 * EVX_MB * 8);
    std::vector<uint8_t> decoded_rgb(evx_width * evx_height * 3);

    // Open CSV output.
    FILE* csv = stdout;
    if (!output_path.empty()) {
        csv = fopen(output_path.c_str(), "w");
        if (!csv) {
            fprintf(stderr, "Error: Cannot open '%s' for writing\n", output_path.c_str());
            destroy_decoder(decoder);
            demuxer.close();
            return 1;
        }
    }

    // Write CSV header.
    fprintf(csv, "frame");
    if (do_psnr) fprintf(csv, ",psnr");
    if (do_ssim) fprintf(csv, ",ssim");
    fprintf(csv, ",encoded_bytes\n");

    // Process packets.
    RGBFrame src_frame;
    double psnr_sum = 0, ssim_sum = 0;
    uint64_t total_bytes = 0;
    uint64_t frame_index = 0;

#if EVX_ENABLE_B_FRAMES
    // With B-frames, decode output is reordered relative to input packets.
    // Buffer all source frames so we can match them to display-order output.
    std::vector<RGBFrame> src_frames;
    std::vector<uint32_t> frame_sizes;
#endif

    evx_demux_packet pkt;
    while (demuxer.read_packet(&pkt) == EVX_SUCCESS) {
        // Skip audio packets.
        if (pkt.stream_type != EVX_STREAM_TYPE_VIDEO)
            continue;

        // Read matching source frame.
        if (!reader.read_frame(src_frame, err))
            break;

        uint32_t frame_size = (uint32_t)pkt.payload.size();

        coded.empty();
        coded.write_bytes(pkt.payload.data(), (uint32)frame_size);

        if (evx_failed(decoder->decode(&coded, decoded_rgb.data()))) {
            fprintf(stderr, "Error: Decode failed at frame %llu\n",
                    (unsigned long long)frame_index);
            break;
        }

#if EVX_ENABLE_B_FRAMES
        src_frames.push_back(src_frame);
        frame_sizes.push_back(frame_size);

        if (decoder->has_output()) {
            const RGBFrame& ref = src_frames[frame_index];
            uint32_t fsz = frame_sizes[frame_index];

            fprintf(csv, "%llu", (unsigned long long)frame_index);

            if (do_psnr) {
                double psnr = compute_psnr(ref.data.data(), decoded_rgb.data(),
                                           evx_width, evx_height);
                fprintf(csv, ",%.4f", psnr);
                psnr_sum += psnr;
            }

            if (do_ssim) {
                double ssim = compute_ssim(ref.data.data(), decoded_rgb.data(),
                                           evx_width, evx_height);
                fprintf(csv, ",%.6f", ssim);
                ssim_sum += ssim;
            }

            fprintf(csv, ",%u\n", fsz);
            total_bytes += fsz;
            frame_index++;

            if (frame_index % 10 == 0)
                fprintf(stderr, "  Frame %llu...\r", (unsigned long long)frame_index);
        }
#else
        // Compute metrics.
        fprintf(csv, "%llu", (unsigned long long)frame_index);

        if (do_psnr) {
            double psnr = compute_psnr(src_frame.data.data(), decoded_rgb.data(),
                                       evx_width, evx_height);
            fprintf(csv, ",%.4f", psnr);
            psnr_sum += psnr;
        }

        if (do_ssim) {
            double ssim = compute_ssim(src_frame.data.data(), decoded_rgb.data(),
                                       evx_width, evx_height);
            fprintf(csv, ",%.6f", ssim);
            ssim_sum += ssim;
        }

        fprintf(csv, ",%u\n", frame_size);
        total_bytes += frame_size;
        frame_index++;

        if (frame_index % 10 == 0)
            fprintf(stderr, "  Frame %llu...\r", (unsigned long long)frame_index);
#endif
    }

#if EVX_ENABLE_B_FRAMES
    // Drain remaining frames from the display reorder buffer.
    while (true) {
        decoder->flush(decoded_rgb.data());
        if (!decoder->has_output()) break;

        if (frame_index < src_frames.size()) {
            const RGBFrame& ref = src_frames[frame_index];
            uint32_t fsz = frame_sizes[frame_index];

            fprintf(csv, "%llu", (unsigned long long)frame_index);

            if (do_psnr) {
                double psnr = compute_psnr(ref.data.data(), decoded_rgb.data(),
                                           evx_width, evx_height);
                fprintf(csv, ",%.4f", psnr);
                psnr_sum += psnr;
            }

            if (do_ssim) {
                double ssim = compute_ssim(ref.data.data(), decoded_rgb.data(),
                                           evx_width, evx_height);
                fprintf(csv, ",%.6f", ssim);
                ssim_sum += ssim;
            }

            fprintf(csv, ",%u\n", fsz);
            total_bytes += fsz;
            frame_index++;

            if (frame_index % 10 == 0)
                fprintf(stderr, "  Frame %llu...\r", (unsigned long long)frame_index);
        }
    }
#endif

    // Print summary.
    if (frame_index > 0) {
        fprintf(stderr, "\n=== Summary ===\n");
        fprintf(stderr, "Frames: %llu\n", (unsigned long long)frame_index);
        if (do_psnr)
            fprintf(stderr, "Average PSNR: %.4f dB\n", psnr_sum / frame_index);
        if (do_ssim)
            fprintf(stderr, "Average SSIM: %.6f\n", ssim_sum / frame_index);
        fprintf(stderr, "Total encoded: %.1f KB\n", (double)total_bytes / 1024.0);

        if (evx_fps > 0)
            fprintf(stderr, "Average bitrate: %.1f kbps\n",
                    (double)total_bytes * 8.0 / 1000.0 / ((double)frame_index / evx_fps));
    }

    if (csv != stdout) fclose(csv);
    destroy_decoder(decoder);
    demuxer.close();

    return 0;
}
