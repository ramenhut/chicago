#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL.h>

#include "video/evx3.h"
#include "audio/audio.h"
#include "audio/types.h"
#include "audio/config.h"
#include "video/config.h"
#include "muxer.h"
#include "bitstream.h"

using namespace evx;

namespace evx {
void audio_decoder_set_header(evx3_audio_decoder *decoder, const evx_audio_header &header);
}

// ---------------------------------------------------------------------------
// Packet index structures
// ---------------------------------------------------------------------------

struct PacketOffset {
    int64_t file_pos;       // position of packet payload in file
    uint32_t payload_size;
    uint8_t stream_type;
    uint64_t timestamp;     // microseconds
};

// ---------------------------------------------------------------------------
// Audio playback state — flat sample array, cursor read by SDL callback
// ---------------------------------------------------------------------------

struct AudioPlayback {
    std::vector<float> samples;     // all decoded audio, interleaved PCM
    SDL_atomic_t cursor;            // current read position in floats
    int total;                      // total float count
};

static void audio_callback(void* userdata, Uint8* stream, int len) {
    AudioPlayback* ap = (AudioPlayback*)userdata;
    float* out = (float*)stream;
    int count = len / (int)sizeof(float);
    int pos = SDL_AtomicGet(&ap->cursor);

    for (int i = 0; i < count; i++) {
        if (pos < ap->total)
            out[i] = ap->samples[pos++];
        else
            out[i] = 0.0f;
    }

    SDL_AtomicSet(&ap->cursor, pos);
}

// Linear-interpolation resampler for pre-decoded audio.
static std::vector<float> resample_audio(const std::vector<float>& src,
                                          int src_rate, int dst_rate, int channels) {
    if (src_rate == dst_rate || src.empty())
        return src;

    int src_frames = (int)src.size() / channels;
    double ratio = (double)dst_rate / (double)src_rate;
    int dst_frames = (int)(src_frames * ratio + 0.5);
    std::vector<float> dst(dst_frames * channels);

    for (int i = 0; i < dst_frames; i++) {
        double src_pos = i / ratio;
        int idx0 = (int)src_pos;
        int idx1 = idx0 + 1;
        if (idx1 >= src_frames) idx1 = src_frames - 1;
        double frac = src_pos - idx0;

        for (int ch = 0; ch < channels; ch++) {
            float s0 = src[idx0 * channels + ch];
            float s1 = src[idx1 * channels + ch];
            dst[i * channels + ch] = (float)(s0 + (s1 - s0) * frac);
        }
    }

    return dst;
}

// ---------------------------------------------------------------------------
// Pre-scan packets from a raw FILE* (after reading container header)
// ---------------------------------------------------------------------------

static bool scan_packets(FILE* f, long data_start,
                         std::vector<PacketOffset>& all_packets,
                         std::vector<int>& video_indices,
                         std::vector<int>& audio_indices) {
    fseek(f, data_start, SEEK_SET);

    while (true) {
        evx_packet_header pkt_hdr;
        if (fread(&pkt_hdr, sizeof(pkt_hdr), 1, f) != 1)
            break;

        if (pkt_hdr.magic[0] != 'E' || pkt_hdr.magic[1] != 'P')
            break;

        PacketOffset entry;
        entry.file_pos = ftell(f);  // start of payload
        entry.payload_size = pkt_hdr.payload_size;
        entry.stream_type = pkt_hdr.stream_type;
        entry.timestamp = pkt_hdr.timestamp;

        int idx = (int)all_packets.size();
        all_packets.push_back(entry);

        if (pkt_hdr.stream_type == EVX_STREAM_TYPE_VIDEO)
            video_indices.push_back(idx);
        else if (pkt_hdr.stream_type == EVX_STREAM_TYPE_AUDIO)
            audio_indices.push_back(idx);

        fseek(f, (long)pkt_hdr.payload_size, SEEK_CUR);
    }

    return !video_indices.empty();
}

// ---------------------------------------------------------------------------
// Read a packet payload at a given file position
// ---------------------------------------------------------------------------

static bool read_payload(FILE* f, const PacketOffset& pkt, std::vector<uint8_t>& buf) {
    if (pkt.payload_size > buf.size())
        buf.resize(pkt.payload_size);
    fseek(f, (long)pkt.file_pos, SEEK_SET);
    return fread(buf.data(), pkt.payload_size, 1, f) == 1;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.evx>\n", argv[0]);
        fprintf(stderr, "  Space=pause  Left/Right=step  +/-=speed  Q=quit\n");
        return 1;
    }

    std::string input_path = argv[1];

    // Open the file for raw reading.
    FILE* src = fopen(input_path.c_str(), "rb");
    if (!src) {
        fprintf(stderr, "Error: Cannot open '%s'\n", input_path.c_str());
        return 1;
    }

    // Read container header manually (same structs as muxer.h).
    evx_container_header container_hdr;
    if (fread(&container_hdr, sizeof(container_hdr), 1, src) != 1 ||
        container_hdr.magic[0] != 'E' || container_hdr.magic[1] != 'V' ||
        container_hdr.magic[2] != 'X' || container_hdr.magic[3] != '3') {
        fprintf(stderr, "Error: Not a valid EVX3 container\n");
        fclose(src);
        return 1;
    }

    // Read stream descriptors.
    std::vector<evx_stream_descriptor> streams(container_hdr.stream_count);
    for (int i = 0; i < container_hdr.stream_count; i++) {
        if (fread(&streams[i], sizeof(evx_stream_descriptor), 1, src) != 1) {
            fprintf(stderr, "Error: Failed to read stream descriptor %d\n", i);
            fclose(src);
            return 1;
        }
    }

    // Find video and audio streams.
    int vid_idx = -1, aud_idx = -1;
    int width = 0, height = 0;
    double frame_rate = 30.0;
    int aud_rate = 0, aud_channels = 0;
    uint8 aud_quality = 0;

    for (int i = 0; i < (int)streams.size(); i++) {
        if (streams[i].stream_type == EVX_STREAM_TYPE_VIDEO && vid_idx < 0) {
            vid_idx = i;
            width = streams[i].width;
            height = streams[i].height;
            frame_rate = streams[i].frame_rate;
        } else if (streams[i].stream_type == EVX_STREAM_TYPE_AUDIO && aud_idx < 0) {
            aud_idx = i;
            aud_rate = streams[i].sample_rate;
            aud_channels = streams[i].channels;
            aud_quality = streams[i].quality;
        }
    }

    if (vid_idx < 0) {
        fprintf(stderr, "Error: No video stream found\n");
        fclose(src);
        return 1;
    }

    if (frame_rate <= 0) frame_rate = 30.0;
    bool has_audio = (aud_idx >= 0);

    fprintf(stderr, "Stream info: video %dx%d @ %.2f fps", width, height, frame_rate);
    if (has_audio)
        fprintf(stderr, ", audio %d Hz %d ch quality=%d", aud_rate, aud_channels, aud_quality);
    fprintf(stderr, "\n");

    long data_start = ftell(src);

    // Pre-scan all packet offsets.
    std::vector<PacketOffset> all_packets;
    std::vector<int> video_packet_indices;
    std::vector<int> audio_packet_indices;

    if (!scan_packets(src, data_start, all_packets, video_packet_indices, audio_packet_indices)) {
        fprintf(stderr, "Error: No video packets found\n");
        fclose(src);
        return 1;
    }

    int total_video_frames = (int)video_packet_indices.size();
    fprintf(stderr, "Playing: %dx%d, %.2f fps, %d video packets",
            width, height, frame_rate, total_video_frames);

    // Pre-decode all audio packets into a flat sample array.
    AudioPlayback audio_pb;
    SDL_AtomicSet(&audio_pb.cursor, 0);
    audio_pb.total = 0;
    evx3_audio_decoder* aud_dec = nullptr;

    if (has_audio && !audio_packet_indices.empty()) {
        create_audio_decoder(&aud_dec);
        evx_audio_header ahdr;
        initialize_audio_header((uint32)aud_rate, (uint8)aud_channels, &ahdr);
        ahdr.quality = aud_quality;
        audio_decoder_set_header(aud_dec, ahdr);

        bit_stream aud_coded;
        aud_coded.resize_capacity(1 * EVX_MB * 8);
        std::vector<uint8_t> aud_payload(1 * EVX_MB);
        const int hop = EVX_AUDIO_HOP_SIZE;
        std::vector<float> pcm_buf(hop * aud_channels);

        // Reserve approximate space.
        audio_pb.samples.reserve(audio_packet_indices.size() * hop * aud_channels);

        int decode_ok = 0, decode_fail = 0;
        for (int i = 0; i < (int)audio_packet_indices.size(); i++) {
            const PacketOffset& apkt = all_packets[audio_packet_indices[i]];
            if (!read_payload(src, apkt, aud_payload))
                break;

            aud_coded.empty();
            aud_coded.write_bytes(aud_payload.data(), apkt.payload_size);

            uint32 out_count = 0;
            if (evx_succeeded(aud_dec->decode(&aud_coded, pcm_buf.data(), &out_count))) {
                audio_pb.samples.insert(audio_pb.samples.end(),
                                        pcm_buf.begin(),
                                        pcm_buf.begin() + out_count * aud_channels);
                decode_ok++;
                if (decode_ok <= 3)
                    fprintf(stderr, "  audio pkt %d: payload=%u bytes, out_count=%u, first sample=%.6f\n",
                            i, apkt.payload_size, out_count, pcm_buf[0]);
            } else {
                decode_fail++;
            }
        }

        audio_pb.total = (int)audio_pb.samples.size();
        double audio_duration = (double)audio_pb.total / (aud_rate * aud_channels);
        double video_duration = total_video_frames / frame_rate;

        fprintf(stderr, "Audio decode: %d ok, %d failed, %d total packets\n",
                decode_ok, decode_fail, (int)audio_packet_indices.size());
        fprintf(stderr, "Audio: %d samples (%.2fs), Video: %.2fs\n",
                audio_pb.total, audio_duration, video_duration);

        fprintf(stderr, ", %d audio samples (%.2fs, %d Hz %d ch)",
                audio_pb.total, audio_duration, aud_rate, aud_channels);
    }

    fprintf(stderr, "\n");

    // Create video decoder.
    evx3_decoder* vid_decoder = nullptr;
    if (evx_failed(create_decoder(&vid_decoder))) {
        fprintf(stderr, "Error: Failed to create video decoder\n");
        if (aud_dec) destroy_audio_decoder(aud_dec);
        fclose(src);
        return 1;
    }

    bit_stream chicago_stream;
    chicago_stream.resize_capacity((4 * EVX_MB) << 3);
    std::vector<uint8_t> payload_buf(4 * EVX_MB);
    std::vector<uint8_t> rgb_buffer(width * height * 3);

    // Initialize SDL2.
    Uint32 sdl_flags = SDL_INIT_VIDEO;
    if (has_audio && audio_pb.total > 0)
        sdl_flags |= SDL_INIT_AUDIO;

    if (SDL_Init(sdl_flags) < 0) {
        fprintf(stderr, "Error: SDL_Init failed: %s\n", SDL_GetError());
        destroy_decoder(vid_decoder);
        if (aud_dec) destroy_audio_decoder(aud_dec);
        fclose(src);
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "EVX Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Error: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        destroy_decoder(vid_decoder);
        if (aud_dec) destroy_audio_decoder(aud_dec);
        fclose(src);
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "Error: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        destroy_decoder(vid_decoder);
        if (aud_dec) destroy_audio_decoder(aud_dec);
        fclose(src);
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        fprintf(stderr, "Error: SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        destroy_decoder(vid_decoder);
        if (aud_dec) destroy_audio_decoder(aud_dec);
        fclose(src);
        return 1;
    }

    // Set up SDL2 audio device.
    SDL_AudioDeviceID audio_dev = 0;

    if (has_audio && audio_pb.total > 0) {
        SDL_AudioSpec want, have;
        SDL_memset(&want, 0, sizeof(want));
        want.freq = aud_rate;
        want.format = AUDIO_F32SYS;
        want.channels = (Uint8)aud_channels;
        want.samples = 1024;
        want.callback = audio_callback;
        want.userdata = &audio_pb;

        audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audio_dev == 0) {
            fprintf(stderr, "Warning: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
            has_audio = false;
        } else {
            fprintf(stderr, "SDL audio: want freq=%d ch=%d fmt=0x%04X samples=%d\n",
                    want.freq, want.channels, want.format, want.samples);
            fprintf(stderr, "SDL audio: have freq=%d ch=%d fmt=0x%04X samples=%d\n",
                    have.freq, have.channels, have.format, have.samples);
            if (have.freq != want.freq) {
                fprintf(stderr, "Device rate %d Hz differs from source %d Hz — resampling audio\n",
                        have.freq, want.freq);
                audio_pb.samples = resample_audio(audio_pb.samples, want.freq, have.freq, aud_channels);
                audio_pb.total = (int)audio_pb.samples.size();
                double new_duration = (double)audio_pb.total / (have.freq * aud_channels);
                fprintf(stderr, "Resampled: %d samples (%.2fs at %d Hz)\n",
                        audio_pb.total, new_duration, have.freq);
            }
            if (have.channels != want.channels || have.format != want.format) {
                fprintf(stderr, "WARNING: SDL audio device channels/format differ from requested!\n");
            }
        }
    }

    // Decode video frame helper (single frame, sequential from decoder state).
    bool display_updated = true;

    auto decode_video_at = [&](int pkt_idx) -> bool {
        const PacketOffset& vpkt = all_packets[video_packet_indices[pkt_idx]];
        if (vpkt.payload_size == 0) {
            display_updated = false;
            return true;
        }
        if (!read_payload(src, vpkt, payload_buf))
            return false;
        chicago_stream.empty();
        chicago_stream.write_bytes(payload_buf.data(), vpkt.payload_size);
        if (evx_failed(vid_decoder->decode(&chicago_stream, rgb_buffer.data())))
            return false;
#if EVX_ENABLE_B_FRAMES
        display_updated = vid_decoder->has_output();
#else
        display_updated = true;
#endif
        return true;
    };

    // Decode from frame 0 to target (for seeking).
    auto decode_to_frame = [&](int target) -> bool {
        vid_decoder->clear();
        for (int i = 0; i <= target; i++) {
            if (!decode_video_at(i))
                return false;
        }
#if EVX_ENABLE_B_FRAMES
        // Flush to ensure the target display-order frame is emitted
        // (it may still be in the reorder buffer)
        while (!vid_decoder->has_output()) {
            vid_decoder->flush(rgb_buffer.data());
            if (!vid_decoder->has_output()) break;
        }
#endif
        return true;
    };

    // Playback state.
    int current_frame = -1;
    bool paused = false;
    float speed = 1.0f;
    bool quit = false;

    // Decode frame 0.
    vid_decoder->clear();
    if (decode_video_at(0))
        current_frame = 0;

    // A/V sync state — wall clock is master, audio cursor is synced to it.
    Uint64 playback_start_ticks = SDL_GetPerformanceCounter();
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    bool has_audio_playback = (audio_dev != 0 && audio_pb.total > 0);

    // Helper: compute the current playback clock in microseconds from wall clock.
    auto get_playback_clock_us = [&]() -> uint64_t {
        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed_s = (double)(now - playback_start_ticks) / (double)perf_freq;
        return (uint64_t)(elapsed_s * speed * 1e6);
    };

    // Start audio playback.
    if (audio_dev)
        SDL_PauseAudioDevice(audio_dev, 0);

    // Track original aspect ratio for enforced resize.
    float aspect_ratio = (float)width / (float)height;

    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = true;
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_RESIZED) {
                int new_w = event.window.data1;
                int new_h = event.window.data2;
                // Determine which dimension the user is primarily dragging
                // by comparing the new aspect ratio to the target.
                float new_aspect = (float)new_w / (float)new_h;
                if (new_aspect > aspect_ratio) {
                    // Window is too wide — adjust width to match height.
                    new_w = (int)(new_h * aspect_ratio + 0.5f);
                } else {
                    // Window is too tall — adjust height to match width.
                    new_h = (int)(new_w / aspect_ratio + 0.5f);
                }
                SDL_SetWindowSize(window, new_w, new_h);
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_q:
                case SDLK_ESCAPE:
                    quit = true;
                    break;
                case SDLK_SPACE:
                    paused = !paused;
                    if (audio_dev)
                        SDL_PauseAudioDevice(audio_dev, paused ? 1 : 0);
                    if (!paused) {
                        // Reset wall clock to current frame's timestamp.
                        playback_start_ticks = SDL_GetPerformanceCounter();
                        if (current_frame >= 0 && current_frame < total_video_frames) {
                            uint64_t cur_ts = all_packets[video_packet_indices[current_frame]].timestamp;
                            playback_start_ticks -= (Uint64)(cur_ts / 1e6 * perf_freq / (double)speed);
                        }
                    }
                    break;
                case SDLK_RIGHT:
                    if (paused && current_frame + 1 < total_video_frames) {
                        if (decode_video_at(current_frame + 1))
                            current_frame++;
#if EVX_ENABLE_B_FRAMES
                        // Keep decoding until we get a displayable frame
                        while (!display_updated && current_frame + 1 < total_video_frames) {
                            if (decode_video_at(current_frame + 1))
                                current_frame++;
                            else
                                break;
                        }
#endif
                    }
                    break;
                case SDLK_LEFT:
                    if (paused && current_frame > 0) {
                        decode_to_frame(current_frame - 1);
                        current_frame--;
                    }
                    break;
                case SDLK_EQUALS:
                case SDLK_PLUS:
                    speed *= 2.0f;
                    if (speed > 8.0f) speed = 8.0f;
                    playback_start_ticks = SDL_GetPerformanceCounter();
                    if (current_frame >= 0) {
                        uint64_t cur_ts = all_packets[video_packet_indices[current_frame]].timestamp;
                        playback_start_ticks -= (Uint64)(cur_ts / 1e6 * perf_freq / (double)speed);
                    }
                    break;
                case SDLK_MINUS:
                    speed /= 2.0f;
                    if (speed < 0.125f) speed = 0.125f;
                    playback_start_ticks = SDL_GetPerformanceCounter();
                    if (current_frame >= 0) {
                        uint64_t cur_ts = all_packets[video_packet_indices[current_frame]].timestamp;
                        playback_start_ticks -= (Uint64)(cur_ts / 1e6 * perf_freq / (double)speed);
                    }
                    break;
                default:
                    break;
                }
            }
        }

        if (!paused) {
            uint64_t playback_clock_us = get_playback_clock_us();

            // Advance video: decode frames until we reach the current clock.
            // Use a budget to avoid blocking the main loop for too long.
            int frames_decoded = 0;
            const int max_frames_per_iter = 4;

            while (current_frame + 1 < total_video_frames && frames_decoded < max_frames_per_iter) {
                uint64_t next_ts = all_packets[video_packet_indices[current_frame + 1]].timestamp;
                bool should_decode = (next_ts <= playback_clock_us);
#if EVX_ENABLE_B_FRAMES
                // Eagerly decode ahead if no output is ready (reorder buffer waiting)
                if (!display_updated) should_decode = true;
#endif
                if (should_decode) {
                    if (decode_video_at(current_frame + 1))
                        current_frame++;
                    else
                        break;
                    frames_decoded++;
                } else {
                    break;
                }
            }

            // If we've reached the end, loop back.
            if (current_frame + 1 >= total_video_frames) {
#if EVX_ENABLE_B_FRAMES
                // Drain remaining buffered frames before resetting decoder
                while (true) {
                    vid_decoder->flush(rgb_buffer.data());
                    if (!vid_decoder->has_output()) break;
                }
#endif
                vid_decoder->clear();
                if (decode_video_at(0)) {
                    current_frame = 0;
                    playback_start_ticks = SDL_GetPerformanceCounter();
                    if (audio_dev)
                        SDL_AtomicSet(&audio_pb.cursor, 0);
                }
            }
        }

        // Update texture and render (skip texture update when decoder has no display frame ready).
#if EVX_ENABLE_B_FRAMES
        if (display_updated)
#endif
        SDL_UpdateTexture(texture, nullptr, rgb_buffer.data(), width * 3);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // Update window title.
        char title[256];
        snprintf(title, sizeof(title), "EVX Player - %s - Frame %d/%d%s [%.1fx]",
                 input_path.c_str(), current_frame + 1, total_video_frames,
                 paused ? " [PAUSED]" : "", speed);
        SDL_SetWindowTitle(window, title);

        SDL_Delay(1);
    }

    // Cleanup.
    if (audio_dev) {
        SDL_PauseAudioDevice(audio_dev, 1);
        SDL_CloseAudioDevice(audio_dev);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    destroy_decoder(vid_decoder);
    if (aud_dec) destroy_audio_decoder(aud_dec);
    fclose(src);

    return 0;
}
