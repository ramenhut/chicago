#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "evx_ffmpeg_util.h"
#include "audio/audio.h"
#include "audio/types.h"
#include "audio/config.h"
#include "bitstream.h"

using namespace evx;
using namespace evx_tools;

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s <input> [options]\n", prog);
    fprintf(stderr, "  -o, --output FILE    Output .evxa file (default: input.evxa)\n");
    fprintf(stderr, "  -q, --quality N      Quality 0-31 (default: 4, 0=best)\n");
    fprintf(stderr, "  --rate N             Target sample rate (default: source)\n");
    fprintf(stderr, "  --channels N         Target channels 1-2 (default: source)\n");
}

static std::string default_output(const std::string& input) {
    size_t dot = input.rfind('.');
    if (dot != std::string::npos)
        return input.substr(0, dot) + ".evxa";
    return input + ".evxa";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;
    int quality = 4;
    int target_rate = 0;
    int target_channels = 0;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if ((arg == "-q" || arg == "--quality") && i + 1 < argc) {
            quality = atoi(argv[++i]);
            if (quality < 0 || quality > 31) {
                fprintf(stderr, "Error: quality must be 0-31\n");
                return 1;
            }
        } else if (arg == "--rate" && i + 1 < argc) {
            target_rate = atoi(argv[++i]);
        } else if (arg == "--channels" && i + 1 < argc) {
            target_channels = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (output_path.empty())
        output_path = default_output(input_path);

    // Open input audio.
    AudioReader reader;
    std::string err = reader.open(input_path, target_rate, target_channels);
    if (!err.empty()) {
        fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }

    const AudioInfo& info = reader.info();
    fprintf(stderr, "Input: %d Hz, %d ch\n", info.sample_rate, info.channels);

    // Create encoder.
    evx3_audio_encoder* encoder = nullptr;
    if (evx_failed(create_audio_encoder((uint32)info.sample_rate, (uint8)info.channels, &encoder))) {
        fprintf(stderr, "Error: Failed to create audio encoder\n");
        return 1;
    }
    encoder->set_quality((uint8)quality);

    // Open output file.
    FILE* dest = fopen(output_path.c_str(), "wb");
    if (!dest) {
        fprintf(stderr, "Error: Cannot open output '%s'\n", output_path.c_str());
        destroy_audio_encoder(encoder);
        return 1;
    }

    // Write audio file header.
    evx_audio_header file_header;
    initialize_audio_header((uint32)info.sample_rate, (uint8)info.channels, &file_header);
    file_header.quality = (uint8)quality;
    fwrite(&file_header, sizeof(file_header), 1, dest);

    bit_stream coded;
    coded.resize_capacity(1 * EVX_MB * 8);

    uint64_t frame_count = 0;
    uint64_t total_bytes = sizeof(evx_audio_header);
    const int hop = EVX_AUDIO_HOP_SIZE;

    std::vector<float> samples;
    while (reader.read_samples(samples, hop, err)) {
        coded.empty();
        evx_status status = encoder->encode(samples.data(), hop, &coded);
        if (evx_failed(status)) {
            fprintf(stderr, "Error: Encode failed at frame %llu\n", (unsigned long long)frame_count);
            break;
        }

        uint32 frame_bytes = coded.query_byte_occupancy();
        fwrite(&frame_bytes, sizeof(frame_bytes), 1, dest);
        fwrite(coded.query_data(), frame_bytes, 1, dest);

        total_bytes += sizeof(frame_bytes) + frame_bytes;
        frame_count++;

        if (frame_count % 100 == 0)
            fprintf(stderr, "  Frame %llu...\r", (unsigned long long)frame_count);
    }

    fclose(dest);
    destroy_audio_encoder(encoder);
    reader.close();

    double duration = (double)(frame_count * hop) / info.sample_rate;
    double bitrate = (duration > 0) ? (total_bytes * 8.0 / 1000.0 / duration) : 0;

    fprintf(stderr, "Done: %llu frames (%.1fs), %.1f KB, %.1f kbps\n",
            (unsigned long long)frame_count, duration,
            (double)total_bytes / 1024.0, bitrate);

    return 0;
}
