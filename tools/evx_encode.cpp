#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "evx_ffmpeg_util.h"
#include "video/evx3.h"
#include "audio/audio.h"
#include "audio/types.h"
#include "audio/config.h"
#include "video/config.h"
#include "muxer.h"
#include "bitstream.h"

using namespace evx;
using namespace evx_tools;

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s <input> [options]\n", prog);
    fprintf(stderr, "  -o, --output FILE    Output .evx file (default: input.evx)\n");
    fprintf(stderr, "  -q, --quality N      Video quality 0-31 (default: 4, 0=best)\n");
    fprintf(stderr, "  --aq N               Audio quality 0-31 (default: 4)\n");
    fprintf(stderr, "  --intra-only         Force all I-frames\n");
    fprintf(stderr, "  --no-audio           Strip audio from output\n");
    fprintf(stderr, "  --width W            Resize width (rounded up to multiple of 16)\n");
    fprintf(stderr, "  --height H           Resize height (rounded up to multiple of 16)\n");
}

static int round_up_16(int val) {
    return (val + 15) & ~15;
}

static std::string default_output(const std::string& input) {
    size_t dot = input.rfind('.');
    if (dot != std::string::npos)
        return input.substr(0, dot) + ".evx";
    return input + ".evx";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;
    int video_quality = 4;
    int audio_quality = 4;
    bool intra_only = false;
    bool no_audio = false;
    int target_width = 0;
    int target_height = 0;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if ((arg == "-q" || arg == "--quality") && i + 1 < argc) {
            video_quality = atoi(argv[++i]);
            if (video_quality < 0 || video_quality > 31) {
                fprintf(stderr, "Error: quality must be 0-31\n");
                return 1;
            }
        } else if (arg == "--aq" && i + 1 < argc) {
            audio_quality = atoi(argv[++i]);
            if (audio_quality < 0 || audio_quality > 31) {
                fprintf(stderr, "Error: audio quality must be 0-31\n");
                return 1;
            }
        } else if (arg == "--intra-only") {
            intra_only = true;
        } else if (arg == "--no-audio") {
            no_audio = true;
        } else if (arg == "--width" && i + 1 < argc) {
            target_width = atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            target_height = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (output_path.empty())
        output_path = default_output(input_path);

    // Open video stream.
    VideoReader vid_reader;
    std::string err;
    if (target_width > 0 && target_height > 0)
        err = vid_reader.open(input_path, target_width, target_height);
    else
        err = vid_reader.open(input_path);

    if (!err.empty()) {
        fprintf(stderr, "Error (video): %s\n", err.c_str());
        return 1;
    }

    const VideoInfo& vinfo = vid_reader.info();

    // Round up to multiples of 16.
    int enc_w = round_up_16(vinfo.width);
    int enc_h = round_up_16(vinfo.height);

    if (enc_w != vinfo.width || enc_h != vinfo.height) {
        fprintf(stderr, "Note: Rounding dimensions from %dx%d to %dx%d (multiple of 16)\n",
                vinfo.width, vinfo.height, enc_w, enc_h);
        vid_reader.close();
        err = vid_reader.open(input_path, enc_w, enc_h);
        if (!err.empty()) {
            fprintf(stderr, "Error (video resize): %s\n", err.c_str());
            return 1;
        }
    }

    // Open audio stream (unless --no-audio).
    AudioReader aud_reader;
    bool has_audio = false;
    if (!no_audio) {
        err = aud_reader.open(input_path);
        has_audio = err.empty();
        if (!has_audio)
            fprintf(stderr, "Note: No audio stream (%s)\n", err.c_str());
    }

    const AudioInfo& ainfo = aud_reader.info();

    // Create encoders.
    evx3_encoder* vid_enc = nullptr;
    create_encoder(&vid_enc);
    vid_enc->set_quality((uint8)video_quality);

    evx3_audio_encoder* aud_enc = nullptr;
    if (has_audio) {
        create_audio_encoder((uint32)ainfo.sample_rate, (uint8)ainfo.channels, &aud_enc);
        aud_enc->set_quality((uint8)audio_quality);
    }

    // Set up muxer.
    evx_muxer muxer;
    if (evx_failed(muxer.open(output_path.c_str()))) {
        fprintf(stderr, "Error: Cannot open output '%s'\n", output_path.c_str());
        return 1;
    }

    uint8 vid_stream_id = 0, aud_stream_id = 0;
    muxer.add_video_stream((uint16)enc_w, (uint16)enc_h, (float32)vinfo.frame_rate, &vid_stream_id);
    if (has_audio) {
        muxer.add_audio_stream((uint32)ainfo.sample_rate, (uint8)ainfo.channels, 16,
                               (uint8)audio_quality, &aud_stream_id);
    }
    muxer.write_header();

    fprintf(stderr, "Encoding %dx%d @ %.2f fps, vq=%d%s",
            enc_w, enc_h, vinfo.frame_rate, video_quality,
            intra_only ? " (intra-only)" : "");
    if (has_audio)
        fprintf(stderr, ", audio %d Hz %d ch aq=%d", ainfo.sample_rate, ainfo.channels, audio_quality);
    fprintf(stderr, "\n");

    bit_stream coded;
    coded.resize_capacity(4 * EVX_MB * 8);

    RGBFrame vid_frame;
    uint64_t src_frame_count = 0;
    uint64_t vid_frame_count = 0;
    uint64_t aud_frame_count = 0;
    const int hop = EVX_AUDIO_HOP_SIZE;

    // Compute audio frames needed per video frame.
    double audio_frames_per_video = 0;
    if (has_audio)
        audio_frames_per_video = (ainfo.sample_rate / (double)hop) / vinfo.frame_rate;

    double audio_frame_accum = 0;
#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
    bool vid_header_written = false;
#endif

    while (vid_reader.read_frame(vid_frame, err)) {
        if (intra_only)
            vid_enc->insert_intra();

        // Encode video frame.
        coded.empty();
        vid_enc->encode(vid_frame.data.data(), (uint32)enc_w, (uint32)enc_h, &coded);
#if EVX_ENABLE_B_FRAMES && (EVX_B_FRAME_COUNT > 0)
        // With B-frames, encode() may output 0 or N frames.
        // The first output includes the evx_header before the framed data, so write
        // it as a single packet.  Subsequent outputs are pure framed data that we
        // split into one muxer packet per frame.
        if (coded.query_byte_occupancy() > 0) {
            if (!vid_header_written) {
                // First output: [evx_header][4-byte size][frame_data] — one I-frame.
                uint64 vid_ts = (uint64)(vid_frame_count * 1000000.0 / vinfo.frame_rate);
                muxer.write_packet(vid_stream_id, EVX_STREAM_TYPE_VIDEO, vid_ts,
                                   coded.query_data(), coded.query_byte_occupancy());
                vid_frame_count++;
                vid_header_written = true;
            } else {
                // Split multi-frame output into individual muxer packets.
                uint8 *base = coded.query_data();
                uint32 read_pos = 0;
                uint32 total_bytes = coded.query_byte_occupancy();
                while (read_pos + 4 <= total_bytes) {
                    uint32 frame_payload_size;
                    memcpy(&frame_payload_size, base + read_pos, sizeof(uint32));
                    uint32 frame_total = 4 + frame_payload_size;
                    if (read_pos + frame_total > total_bytes) break;
                    uint64 vid_ts = (uint64)(vid_frame_count * 1000000.0 / vinfo.frame_rate);
                    muxer.write_packet(vid_stream_id, EVX_STREAM_TYPE_VIDEO, vid_ts,
                                       base + read_pos, frame_total);
                    vid_frame_count++;
                    read_pos += frame_total;
                }
            }
        }
#else
        {
            uint64 vid_ts = (uint64)(vid_frame_count * 1000000.0 / vinfo.frame_rate);
            muxer.write_packet(vid_stream_id, EVX_STREAM_TYPE_VIDEO, vid_ts,
                               coded.query_data(), coded.query_byte_occupancy());
            vid_frame_count++;
        }
#endif

        // Encode corresponding audio frames.
        if (has_audio) {
            audio_frame_accum += audio_frames_per_video;
            while (audio_frame_accum >= 1.0) {
                std::vector<float> samples;
                if (!aud_reader.read_samples(samples, hop, err))
                    break;

                coded.empty();
                aud_enc->encode(samples.data(), hop, &coded);
                uint64 aud_ts = (uint64)(aud_frame_count * hop * 1000000.0 / ainfo.sample_rate);
                muxer.write_packet(aud_stream_id, EVX_STREAM_TYPE_AUDIO, aud_ts,
                                   coded.query_data(), coded.query_byte_occupancy());
                aud_frame_count++;
                audio_frame_accum -= 1.0;
            }
        }

        src_frame_count++;
        if (src_frame_count % 10 == 0)
            fprintf(stderr, "  Frame %llu...\r", (unsigned long long)src_frame_count);
    }

    if (!err.empty())
        fprintf(stderr, "Warning: %s\n", err.c_str());

#if EVX_ENABLE_B_FRAMES
    {
        coded.empty();
        vid_enc->flush(&coded);
        uint8 *base = coded.query_data();
        uint32 read_pos = 0;
        uint32 total_bytes = coded.query_byte_occupancy();
        while (read_pos + 4 <= total_bytes) {
            uint32 frame_payload_size;
            memcpy(&frame_payload_size, base + read_pos, sizeof(uint32));
            uint32 frame_total = 4 + frame_payload_size;
            if (read_pos + frame_total > total_bytes) break;
            uint64 vid_ts = (uint64)(vid_frame_count * 1000000.0 / vinfo.frame_rate);
            muxer.write_packet(vid_stream_id, EVX_STREAM_TYPE_VIDEO, vid_ts,
                               base + read_pos, frame_total);
            vid_frame_count++;
            read_pos += frame_total;
        }
    }
#endif

    muxer.close();
    destroy_encoder(vid_enc);
    if (aud_enc) destroy_audio_encoder(aud_enc);
    vid_reader.close();
    aud_reader.close();

    fprintf(stderr, "Done: %llu video + %llu audio frames\n",
            (unsigned long long)vid_frame_count, (unsigned long long)aud_frame_count);

    return 0;
}
