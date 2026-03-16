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

namespace evx {
void audio_decoder_set_header(evx3_audio_decoder *decoder, const evx_audio_header &header);
}

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s <input.evxa> [options]\n", prog);
    fprintf(stderr, "  -o, --output FILE    Output audio file (default: input.wav)\n");
}

static std::string default_output(const std::string& input) {
    size_t dot = input.rfind('.');
    if (dot != std::string::npos)
        return input.substr(0, dot) + ".wav";
    return input + ".wav";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (output_path.empty())
        output_path = default_output(input_path);

    // Open .evxa input.
    FILE* src = fopen(input_path.c_str(), "rb");
    if (!src) {
        fprintf(stderr, "Error: Cannot open '%s'\n", input_path.c_str());
        return 1;
    }

    // Read audio header.
    evx_audio_header file_header;
    if (fread(&file_header, sizeof(file_header), 1, src) != 1) {
        fprintf(stderr, "Error: Failed to read audio header\n");
        fclose(src);
        return 1;
    }

    if (evx_failed(verify_audio_header(file_header))) {
        fprintf(stderr, "Error: Not a valid EVX audio file\n");
        fclose(src);
        return 1;
    }

    fprintf(stderr, "Input: %u Hz, %d ch, quality=%d\n",
            file_header.sample_rate, file_header.channels, file_header.quality);

    // Create decoder.
    evx3_audio_decoder* decoder = nullptr;
    if (evx_failed(create_audio_decoder(&decoder))) {
        fprintf(stderr, "Error: Failed to create audio decoder\n");
        fclose(src);
        return 1;
    }
    audio_decoder_set_header(decoder, file_header);

    // Open output writer.
    AudioWriter writer;
    AudioWriter::Config config;
    config.path = output_path;
    config.sample_rate = (int)file_header.sample_rate;
    config.channels = (int)file_header.channels;

    std::string err = writer.open(config);
    if (!err.empty()) {
        fprintf(stderr, "Error: %s\n", err.c_str());
        destroy_audio_decoder(decoder);
        fclose(src);
        return 1;
    }

    bit_stream coded;
    coded.resize_capacity(1 * EVX_MB * 8);

    const int hop = EVX_AUDIO_HOP_SIZE;
    int channels = file_header.channels;
    std::vector<float> pcm_out(hop * channels);
    uint64_t frame_count = 0;

    while (true) {
        uint32_t frame_bytes = 0;
        if (fread(&frame_bytes, sizeof(frame_bytes), 1, src) != 1)
            break;  // EOF

        if (frame_bytes == 0 || frame_bytes > 4 * EVX_MB) {
            fprintf(stderr, "Error: Invalid frame size %u at frame %llu\n",
                    frame_bytes, (unsigned long long)frame_count);
            break;
        }

        std::vector<uint8_t> payload(frame_bytes);
        if (fread(payload.data(), frame_bytes, 1, src) != 1) {
            fprintf(stderr, "Error: Short read at frame %llu\n",
                    (unsigned long long)frame_count);
            break;
        }

        coded.empty();
        coded.write_bytes(payload.data(), frame_bytes);

        uint32 out_count = 0;
        evx_status status = decoder->decode(&coded, pcm_out.data(), &out_count);
        if (evx_failed(status)) {
            fprintf(stderr, "Error: Decode failed at frame %llu\n",
                    (unsigned long long)frame_count);
            break;
        }

        err = writer.write_samples(pcm_out.data(), hop);
        if (!err.empty()) {
            fprintf(stderr, "Error: %s\n", err.c_str());
            break;
        }

        frame_count++;
        if (frame_count % 100 == 0)
            fprintf(stderr, "  Frame %llu...\r", (unsigned long long)frame_count);
    }

    err = writer.finalize();
    if (!err.empty())
        fprintf(stderr, "Warning: %s\n", err.c_str());

    fclose(src);
    destroy_audio_decoder(decoder);

    double duration = (double)(frame_count * hop) / file_header.sample_rate;
    fprintf(stderr, "Done: %llu frames (%.1fs) decoded to '%s'\n",
            (unsigned long long)frame_count, duration, output_path.c_str());

    return 0;
}
