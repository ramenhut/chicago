#include <cstdio>
#include <cstdlib>
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

namespace evx {
void audio_decoder_set_header(evx3_audio_decoder *decoder, const evx_audio_header &header);
}

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s <input.evx> [options]\n", prog);
    fprintf(stderr, "  -o, --output FILE    Output video file (default: input.mp4)\n");
    fprintf(stderr, "  --audio FILE         Output audio file (default: input.wav)\n");
}

static std::string stem_of(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot != std::string::npos)
        return path.substr(0, dot);
    return path;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_path = argv[1];
    std::string video_output;
    std::string audio_output;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            video_output = argv[++i];
        } else if (arg == "--audio" && i + 1 < argc) {
            audio_output = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (video_output.empty()) video_output = stem_of(input_path) + ".mp4";
    if (audio_output.empty()) audio_output = stem_of(input_path) + ".wav";

    // Open container.
    evx_demuxer demuxer;
    if (evx_failed(demuxer.open(input_path.c_str()))) {
        fprintf(stderr, "Error: Cannot open '%s'\n", input_path.c_str());
        return 1;
    }

    if (evx_failed(demuxer.read_header())) {
        fprintf(stderr, "Error: Invalid container header\n");
        demuxer.close();
        return 1;
    }

    // Find video and audio streams.
    int vid_stream_id = -1, aud_stream_id = -1;
    int vid_w = 0, vid_h = 0;
    double vid_fps = 30.0;
    int aud_rate = 0, aud_channels = 0;
    uint8 aud_quality = 0;

    for (uint8 i = 0; i < demuxer.query_stream_count(); i++) {
        const evx_stream_descriptor& sd = demuxer.query_stream(i);
        if (sd.stream_type == EVX_STREAM_TYPE_VIDEO && vid_stream_id < 0) {
            vid_stream_id = sd.stream_id;
            vid_w = sd.width;
            vid_h = sd.height;
            vid_fps = sd.frame_rate;
        } else if (sd.stream_type == EVX_STREAM_TYPE_AUDIO && aud_stream_id < 0) {
            aud_stream_id = sd.stream_id;
            aud_rate = sd.sample_rate;
            aud_channels = sd.channels;
            aud_quality = sd.quality;
        }
    }

    fprintf(stderr, "Container: %d streams", demuxer.query_stream_count());
    if (vid_stream_id >= 0)
        fprintf(stderr, ", video %dx%d @ %.2f fps", vid_w, vid_h, vid_fps);
    if (aud_stream_id >= 0)
        fprintf(stderr, ", audio %d Hz %d ch", aud_rate, aud_channels);
    fprintf(stderr, "\n");

    // Create decoders.
    evx3_decoder* vid_dec = nullptr;
    evx3_audio_decoder* aud_dec = nullptr;

    if (vid_stream_id >= 0)
        create_decoder(&vid_dec);

    if (aud_stream_id >= 0) {
        create_audio_decoder(&aud_dec);
        evx_audio_header ahdr;
        initialize_audio_header((uint32)aud_rate, (uint8)aud_channels, &ahdr);
        ahdr.quality = aud_quality;
        audio_decoder_set_header(aud_dec, ahdr);
    }

    // Open output writers.
    VideoWriter vid_writer;
    AudioWriter aud_writer;
    std::string err;

    if (vid_stream_id >= 0) {
        VideoWriter::Config vc;
        vc.path = video_output;
        vc.width = vid_w;
        vc.height = vid_h;
        vc.frame_rate = vid_fps;
        err = vid_writer.open(vc);
        if (!err.empty()) {
            fprintf(stderr, "Error (video writer): %s\n", err.c_str());
            return 1;
        }
    }

    if (aud_stream_id >= 0) {
        AudioWriter::Config ac;
        ac.path = audio_output;
        ac.sample_rate = aud_rate;
        ac.channels = aud_channels;
        err = aud_writer.open(ac);
        if (!err.empty()) {
            fprintf(stderr, "Error (audio writer): %s\n", err.c_str());
            return 1;
        }
    }

    // Decode packets.
    bit_stream coded;
    coded.resize_capacity(4 * EVX_MB * 8);

    uint64_t vid_count = 0, aud_count = 0;
    std::vector<uint8_t> rgb_buf(vid_w * vid_h * 3);
    const int hop = EVX_AUDIO_HOP_SIZE;
    std::vector<float> pcm_buf(hop * aud_channels);

    evx_demux_packet pkt;
    while (demuxer.read_packet(&pkt) == EVX_SUCCESS) {
        if (pkt.stream_type == EVX_STREAM_TYPE_VIDEO && vid_dec) {
            coded.empty();
            coded.write_bytes(pkt.payload.data(), (uint32)pkt.payload.size());

            if (evx_succeeded(vid_dec->decode(&coded, rgb_buf.data()))) {
#if EVX_ENABLE_B_FRAMES
                if (vid_dec->has_output())
#endif
                {
                    RGBFrame out_frame;
                    out_frame.width = vid_w;
                    out_frame.height = vid_h;
                    out_frame.data.assign(rgb_buf.begin(), rgb_buf.end());
                    vid_writer.write_frame(out_frame);
                    vid_count++;
                }
            }
        } else if (pkt.stream_type == EVX_STREAM_TYPE_AUDIO && aud_dec) {
            coded.empty();
            coded.write_bytes(pkt.payload.data(), (uint32)pkt.payload.size());

            uint32 out_count = 0;
            if (evx_succeeded(aud_dec->decode(&coded, pcm_buf.data(), &out_count))) {
                aud_writer.write_samples(pcm_buf.data(), hop);
                aud_count++;
            }
        }

        if ((vid_count + aud_count) % 20 == 0)
            fprintf(stderr, "  V:%llu A:%llu...\r",
                    (unsigned long long)vid_count, (unsigned long long)aud_count);
    }

#if EVX_ENABLE_B_FRAMES
    if (vid_dec) {
        while (true) {
            vid_dec->flush(rgb_buf.data());
            if (!vid_dec->has_output()) break;
            RGBFrame out_frame;
            out_frame.width = vid_w;
            out_frame.height = vid_h;
            out_frame.data.assign(rgb_buf.begin(), rgb_buf.end());
            vid_writer.write_frame(out_frame);
            vid_count++;
        }
    }
#endif

    // Finalize.
    if (vid_stream_id >= 0) {
        err = vid_writer.finalize();
        if (!err.empty()) fprintf(stderr, "Warning (video): %s\n", err.c_str());
    }
    if (aud_stream_id >= 0) {
        err = aud_writer.finalize();
        if (!err.empty()) fprintf(stderr, "Warning (audio): %s\n", err.c_str());
    }

    demuxer.close();
    if (vid_dec) destroy_decoder(vid_dec);
    if (aud_dec) destroy_audio_decoder(aud_dec);

    fprintf(stderr, "Done: %llu video frames, %llu audio frames\n",
            (unsigned long long)vid_count, (unsigned long long)aud_count);

    return 0;
}
