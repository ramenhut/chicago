#include "evx_ffmpeg_util.h"

#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace evx_tools {

std::string av_error_string(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// VideoReader
// ---------------------------------------------------------------------------

struct VideoReader::Impl {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgb_frame = nullptr;
    AVPacket* packet = nullptr;
    uint8_t* rgb_buffer = nullptr;
    int video_stream_idx = -1;
    int target_width = 0;
    int target_height = 0;

    ~Impl() { cleanup(); }

    void cleanup() {
        if (rgb_buffer) { av_free(rgb_buffer); rgb_buffer = nullptr; }
        if (packet) { av_packet_free(&packet); packet = nullptr; }
        if (rgb_frame) { av_frame_free(&rgb_frame); rgb_frame = nullptr; }
        if (frame) { av_frame_free(&frame); frame = nullptr; }
        if (sws_ctx) { sws_freeContext(sws_ctx); sws_ctx = nullptr; }
        if (codec_ctx) { avcodec_free_context(&codec_ctx); codec_ctx = nullptr; }
        if (fmt_ctx) { avformat_close_input(&fmt_ctx); fmt_ctx = nullptr; }
        video_stream_idx = -1;
    }
};

VideoReader::VideoReader() : impl_(std::make_unique<Impl>()) {}
VideoReader::~VideoReader() = default;
VideoReader::VideoReader(VideoReader&& other) noexcept = default;
VideoReader& VideoReader::operator=(VideoReader&& other) noexcept = default;

const VideoInfo& VideoReader::info() const { return info_; }

std::string VideoReader::open(const std::string& path) {
    return open(path, 0, 0);
}

std::string VideoReader::open(const std::string& path, int target_width, int target_height) {
    close();

    int ret = avformat_open_input(&impl_->fmt_ctx, path.c_str(), nullptr, nullptr);
    if (ret < 0)
        return "Failed to open '" + path + "': " + av_error_string(ret);

    ret = avformat_find_stream_info(impl_->fmt_ctx, nullptr);
    if (ret < 0) {
        close();
        return "Failed to find stream info: " + av_error_string(ret);
    }

    // Find the best video stream.
    const AVCodec* codec = nullptr;
    impl_->video_stream_idx = av_find_best_stream(
        impl_->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (impl_->video_stream_idx < 0) {
        close();
        return "No video stream found in '" + path + "'";
    }

    AVStream* stream = impl_->fmt_ctx->streams[impl_->video_stream_idx];

    // Allocate codec context.
    impl_->codec_ctx = avcodec_alloc_context3(codec);
    if (!impl_->codec_ctx) {
        close();
        return "Failed to allocate codec context";
    }

    ret = avcodec_parameters_to_context(impl_->codec_ctx, stream->codecpar);
    if (ret < 0) {
        close();
        return "Failed to copy codec parameters: " + av_error_string(ret);
    }

    ret = avcodec_open2(impl_->codec_ctx, codec, nullptr);
    if (ret < 0) {
        close();
        return "Failed to open codec: " + av_error_string(ret);
    }

    // Determine output dimensions.
    int src_w = impl_->codec_ctx->width;
    int src_h = impl_->codec_ctx->height;
    impl_->target_width = (target_width > 0) ? target_width : src_w;
    impl_->target_height = (target_height > 0) ? target_height : src_h;

    // Set up swscale for conversion to RGB24.
    impl_->sws_ctx = sws_getContext(
        src_w, src_h, impl_->codec_ctx->pix_fmt,
        impl_->target_width, impl_->target_height, AV_PIX_FMT_RGB24,
        SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!impl_->sws_ctx) {
        close();
        return "Failed to create swscale context";
    }

    // Allocate frames.
    impl_->frame = av_frame_alloc();
    impl_->rgb_frame = av_frame_alloc();
    impl_->packet = av_packet_alloc();
    if (!impl_->frame || !impl_->rgb_frame || !impl_->packet) {
        close();
        return "Failed to allocate frame/packet";
    }

    int rgb_size = av_image_get_buffer_size(
        AV_PIX_FMT_RGB24, impl_->target_width, impl_->target_height, 1);
    impl_->rgb_buffer = (uint8_t*)av_malloc(rgb_size);
    if (!impl_->rgb_buffer) {
        close();
        return "Failed to allocate RGB buffer";
    }

    av_image_fill_arrays(impl_->rgb_frame->data, impl_->rgb_frame->linesize,
                         impl_->rgb_buffer, AV_PIX_FMT_RGB24,
                         impl_->target_width, impl_->target_height, 1);

    // Fill in VideoInfo.
    info_.width = impl_->target_width;
    info_.height = impl_->target_height;
    info_.frame_count = stream->nb_frames;
    if (stream->avg_frame_rate.den > 0)
        info_.frame_rate = av_q2d(stream->avg_frame_rate);
    else if (stream->r_frame_rate.den > 0)
        info_.frame_rate = av_q2d(stream->r_frame_rate);
    else
        info_.frame_rate = 30.0;

    return "";
}

bool VideoReader::read_frame(RGBFrame& out, std::string& error) {
    if (!impl_->fmt_ctx) {
        error = "Reader not open";
        return false;
    }

    while (true) {
        int ret = av_read_frame(impl_->fmt_ctx, impl_->packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // Flush the decoder.
                avcodec_send_packet(impl_->codec_ctx, nullptr);
                ret = avcodec_receive_frame(impl_->codec_ctx, impl_->frame);
                if (ret == 0) {
                    // Got a flushed frame — convert and return it.
                    sws_scale(impl_->sws_ctx,
                              impl_->frame->data, impl_->frame->linesize, 0,
                              impl_->codec_ctx->height,
                              impl_->rgb_frame->data, impl_->rgb_frame->linesize);

                    out.width = impl_->target_width;
                    out.height = impl_->target_height;
                    out.data.resize(out.width * out.height * 3);
                    for (int y = 0; y < out.height; y++) {
                        memcpy(out.data.data() + y * out.row_pitch(),
                               impl_->rgb_frame->data[0] + y * impl_->rgb_frame->linesize[0],
                               out.row_pitch());
                    }
                    return true;
                }
                return false;  // True EOF, no more frames.
            }
            error = "Read error: " + av_error_string(ret);
            return false;
        }

        if (impl_->packet->stream_index != impl_->video_stream_idx) {
            av_packet_unref(impl_->packet);
            continue;
        }

        ret = avcodec_send_packet(impl_->codec_ctx, impl_->packet);
        av_packet_unref(impl_->packet);
        if (ret < 0) {
            error = "Send packet error: " + av_error_string(ret);
            return false;
        }

        ret = avcodec_receive_frame(impl_->codec_ctx, impl_->frame);
        if (ret == AVERROR(EAGAIN)) continue;
        if (ret < 0) {
            error = "Receive frame error: " + av_error_string(ret);
            return false;
        }

        // Convert to RGB24.
        sws_scale(impl_->sws_ctx,
                  impl_->frame->data, impl_->frame->linesize, 0,
                  impl_->codec_ctx->height,
                  impl_->rgb_frame->data, impl_->rgb_frame->linesize);

        out.width = impl_->target_width;
        out.height = impl_->target_height;
        out.data.resize(out.width * out.height * 3);
        for (int y = 0; y < out.height; y++) {
            memcpy(out.data.data() + y * out.row_pitch(),
                   impl_->rgb_frame->data[0] + y * impl_->rgb_frame->linesize[0],
                   out.row_pitch());
        }
        return true;
    }
}

bool VideoReader::seek_to_start() {
    if (!impl_->fmt_ctx) return false;
    avcodec_flush_buffers(impl_->codec_ctx);
    return av_seek_frame(impl_->fmt_ctx, impl_->video_stream_idx, 0,
                         AVSEEK_FLAG_BACKWARD) >= 0;
}

void VideoReader::close() {
    impl_->cleanup();
    info_ = {};
}

// ---------------------------------------------------------------------------
// VideoWriter
// ---------------------------------------------------------------------------

struct VideoWriter::Impl {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    AVStream* stream = nullptr;
    int64_t pts = 0;
    bool finalized = false;

    ~Impl() { cleanup(); }

    void cleanup() {
        if (packet) { av_packet_free(&packet); packet = nullptr; }
        if (frame) { av_frame_free(&frame); frame = nullptr; }
        if (sws_ctx) { sws_freeContext(sws_ctx); sws_ctx = nullptr; }
        if (codec_ctx) { avcodec_free_context(&codec_ctx); codec_ctx = nullptr; }
        if (fmt_ctx) {
            if (fmt_ctx->pb) avio_closep(&fmt_ctx->pb);
            avformat_free_context(fmt_ctx);
            fmt_ctx = nullptr;
        }
        stream = nullptr;
        pts = 0;
        finalized = false;
    }
};

VideoWriter::VideoWriter() : impl_(std::make_unique<Impl>()) {}
VideoWriter::~VideoWriter() { close(); }

std::string VideoWriter::open(const Config& config) {
    close();

    int ret = avformat_alloc_output_context2(
        &impl_->fmt_ctx, nullptr, nullptr, config.path.c_str());
    if (!impl_->fmt_ctx)
        return "Could not determine output format for '" + config.path + "'";

    // Find encoder — prefer libx264 for mp4/mkv, fall back to format default.
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)
        codec = avcodec_find_encoder(impl_->fmt_ctx->oformat->video_codec);
    if (!codec) {
        close();
        return "No suitable video encoder found";
    }

    impl_->stream = avformat_new_stream(impl_->fmt_ctx, nullptr);
    if (!impl_->stream) {
        close();
        return "Failed to create output stream";
    }

    impl_->codec_ctx = avcodec_alloc_context3(codec);
    if (!impl_->codec_ctx) {
        close();
        return "Failed to allocate encoder context";
    }

    impl_->codec_ctx->width = config.width;
    impl_->codec_ctx->height = config.height;
    impl_->codec_ctx->time_base = AVRational{1, (int)(config.frame_rate + 0.5)};
    impl_->codec_ctx->framerate = AVRational{(int)(config.frame_rate + 0.5), 1};
    impl_->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    impl_->codec_ctx->gop_size = 12;

    if (impl_->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        impl_->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Set a reasonable CRF for quality.
    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(impl_->codec_ctx->priv_data, "crf", "18", 0);

    ret = avcodec_open2(impl_->codec_ctx, codec, nullptr);
    if (ret < 0) {
        close();
        return "Failed to open encoder: " + av_error_string(ret);
    }

    ret = avcodec_parameters_from_context(impl_->stream->codecpar, impl_->codec_ctx);
    if (ret < 0) {
        close();
        return "Failed to copy encoder parameters: " + av_error_string(ret);
    }

    impl_->stream->time_base = impl_->codec_ctx->time_base;

    // Open output file.
    if (!(impl_->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&impl_->fmt_ctx->pb, config.path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            close();
            return "Failed to open output file '" + config.path + "': " + av_error_string(ret);
        }
    }

    ret = avformat_write_header(impl_->fmt_ctx, nullptr);
    if (ret < 0) {
        close();
        return "Failed to write header: " + av_error_string(ret);
    }

    // Allocate encode frame (YUV420P).
    impl_->frame = av_frame_alloc();
    impl_->frame->format = impl_->codec_ctx->pix_fmt;
    impl_->frame->width = config.width;
    impl_->frame->height = config.height;
    ret = av_frame_get_buffer(impl_->frame, 0);
    if (ret < 0) {
        close();
        return "Failed to allocate frame buffer: " + av_error_string(ret);
    }

    impl_->packet = av_packet_alloc();

    // Set up swscale: RGB24 -> YUV420P.
    impl_->sws_ctx = sws_getContext(
        config.width, config.height, AV_PIX_FMT_RGB24,
        config.width, config.height, impl_->codec_ctx->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!impl_->sws_ctx) {
        close();
        return "Failed to create swscale context for writer";
    }

    return "";
}

std::string VideoWriter::write_frame(const RGBFrame& frame) {
    if (!impl_->fmt_ctx || !impl_->codec_ctx)
        return "Writer not open";

    int ret = av_frame_make_writable(impl_->frame);
    if (ret < 0)
        return "Frame not writable: " + av_error_string(ret);

    // Convert RGB24 input to encoder pixel format.
    const uint8_t* src_data[1] = { frame.data.data() };
    int src_linesize[1] = { frame.row_pitch() };

    sws_scale(impl_->sws_ctx, src_data, src_linesize, 0, frame.height,
              impl_->frame->data, impl_->frame->linesize);

    impl_->frame->pts = impl_->pts++;

    ret = avcodec_send_frame(impl_->codec_ctx, impl_->frame);
    if (ret < 0)
        return "Send frame error: " + av_error_string(ret);

    while (true) {
        ret = avcodec_receive_packet(impl_->codec_ctx, impl_->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0)
            return "Receive packet error: " + av_error_string(ret);

        av_packet_rescale_ts(impl_->packet,
                             impl_->codec_ctx->time_base,
                             impl_->stream->time_base);
        impl_->packet->stream_index = impl_->stream->index;

        ret = av_interleaved_write_frame(impl_->fmt_ctx, impl_->packet);
        if (ret < 0)
            return "Write frame error: " + av_error_string(ret);
    }

    return "";
}

std::string VideoWriter::finalize() {
    if (!impl_->fmt_ctx || !impl_->codec_ctx || impl_->finalized)
        return "";

    impl_->finalized = true;

    // Flush encoder.
    int ret = avcodec_send_frame(impl_->codec_ctx, nullptr);
    if (ret < 0 && ret != AVERROR_EOF)
        return "Flush error: " + av_error_string(ret);

    while (true) {
        ret = avcodec_receive_packet(impl_->codec_ctx, impl_->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0)
            return "Flush receive error: " + av_error_string(ret);

        av_packet_rescale_ts(impl_->packet,
                             impl_->codec_ctx->time_base,
                             impl_->stream->time_base);
        impl_->packet->stream_index = impl_->stream->index;

        av_interleaved_write_frame(impl_->fmt_ctx, impl_->packet);
    }

    ret = av_write_trailer(impl_->fmt_ctx);
    if (ret < 0)
        return "Write trailer error: " + av_error_string(ret);

    return "";
}

void VideoWriter::close() {
    if (impl_->fmt_ctx && !impl_->finalized)
        finalize();
    impl_->cleanup();
}

// ---------------------------------------------------------------------------
// AudioReader
// ---------------------------------------------------------------------------

struct AudioReader::Impl {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int audio_stream_idx = -1;
    int target_sample_rate = 0;
    int target_channels = 0;

    // Residual buffer for partial reads.
    std::vector<float> residual;
    size_t residual_offset = 0;

    ~Impl() { cleanup(); }

    void cleanup() {
        if (packet) { av_packet_free(&packet); packet = nullptr; }
        if (frame) { av_frame_free(&frame); frame = nullptr; }
        if (swr_ctx) { swr_free(&swr_ctx); swr_ctx = nullptr; }
        if (codec_ctx) { avcodec_free_context(&codec_ctx); codec_ctx = nullptr; }
        if (fmt_ctx) { avformat_close_input(&fmt_ctx); fmt_ctx = nullptr; }
        audio_stream_idx = -1;
        residual.clear();
        residual_offset = 0;
    }
};

AudioReader::AudioReader() : impl_(std::make_unique<Impl>()) {}
AudioReader::~AudioReader() = default;
AudioReader::AudioReader(AudioReader&& other) noexcept = default;
AudioReader& AudioReader::operator=(AudioReader&& other) noexcept = default;

const AudioInfo& AudioReader::info() const { return info_; }

std::string AudioReader::open(const std::string& path, int target_sample_rate, int target_channels) {
    close();

    int ret = avformat_open_input(&impl_->fmt_ctx, path.c_str(), nullptr, nullptr);
    if (ret < 0)
        return "Failed to open '" + path + "': " + av_error_string(ret);

    ret = avformat_find_stream_info(impl_->fmt_ctx, nullptr);
    if (ret < 0) {
        close();
        return "Failed to find stream info: " + av_error_string(ret);
    }

    const AVCodec* codec = nullptr;
    impl_->audio_stream_idx = av_find_best_stream(
        impl_->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (impl_->audio_stream_idx < 0) {
        close();
        return "No audio stream found in '" + path + "'";
    }

    AVStream* stream = impl_->fmt_ctx->streams[impl_->audio_stream_idx];

    impl_->codec_ctx = avcodec_alloc_context3(codec);
    if (!impl_->codec_ctx) {
        close();
        return "Failed to allocate audio codec context";
    }

    ret = avcodec_parameters_to_context(impl_->codec_ctx, stream->codecpar);
    if (ret < 0) {
        close();
        return "Failed to copy audio codec parameters: " + av_error_string(ret);
    }

    ret = avcodec_open2(impl_->codec_ctx, codec, nullptr);
    if (ret < 0) {
        close();
        return "Failed to open audio codec: " + av_error_string(ret);
    }

    int src_rate = impl_->codec_ctx->sample_rate;
    int src_ch = impl_->codec_ctx->ch_layout.nb_channels;

    impl_->target_sample_rate = (target_sample_rate > 0) ? target_sample_rate : src_rate;
    impl_->target_channels = (target_channels > 0) ? target_channels : src_ch;

    // Set up swresample for conversion to float32 interleaved.
    AVChannelLayout out_layout = {};
    av_channel_layout_default(&out_layout, impl_->target_channels);

    ret = swr_alloc_set_opts2(&impl_->swr_ctx,
        &out_layout, AV_SAMPLE_FMT_FLT, impl_->target_sample_rate,
        &impl_->codec_ctx->ch_layout, impl_->codec_ctx->sample_fmt, src_rate,
        0, nullptr);
    if (ret < 0) {
        close();
        return "Failed to set up swresample: " + av_error_string(ret);
    }

    ret = swr_init(impl_->swr_ctx);
    if (ret < 0) {
        close();
        return "Failed to init swresample: " + av_error_string(ret);
    }

    impl_->frame = av_frame_alloc();
    impl_->packet = av_packet_alloc();
    if (!impl_->frame || !impl_->packet) {
        close();
        return "Failed to allocate frame/packet";
    }

    info_.sample_rate = impl_->target_sample_rate;
    info_.channels = impl_->target_channels;
    info_.total_samples = (stream->duration != AV_NOPTS_VALUE && stream->time_base.den > 0)
        ? (int64_t)(stream->duration * av_q2d(stream->time_base) * impl_->target_sample_rate)
        : 0;

    return "";
}

bool AudioReader::read_samples(std::vector<float>& out, int frame_size, std::string& error) {
    if (!impl_->fmt_ctx) {
        error = "Reader not open";
        return false;
    }

    int channels = impl_->target_channels;
    int needed = frame_size * channels;
    out.resize(needed);
    int filled = 0;

    // Drain residual first.
    size_t avail = impl_->residual.size() - impl_->residual_offset;
    if (avail > 0) {
        int copy = (avail > (size_t)needed) ? needed : (int)avail;
        memcpy(out.data(), impl_->residual.data() + impl_->residual_offset, copy * sizeof(float));
        impl_->residual_offset += copy;
        filled += copy;
        if (impl_->residual_offset >= impl_->residual.size()) {
            impl_->residual.clear();
            impl_->residual_offset = 0;
        }
    }

    while (filled < needed) {
        int ret = av_read_frame(impl_->fmt_ctx, impl_->packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // Pad with silence if we got partial data.
                if (filled > 0) {
                    memset(out.data() + filled, 0, (needed - filled) * sizeof(float));
                    return true;
                }
                return false;
            }
            error = "Read error: " + av_error_string(ret);
            return false;
        }

        if (impl_->packet->stream_index != impl_->audio_stream_idx) {
            av_packet_unref(impl_->packet);
            continue;
        }

        ret = avcodec_send_packet(impl_->codec_ctx, impl_->packet);
        av_packet_unref(impl_->packet);
        if (ret < 0) {
            error = "Send packet error: " + av_error_string(ret);
            return false;
        }

        while (true) {
            ret = avcodec_receive_frame(impl_->codec_ctx, impl_->frame);
            if (ret == AVERROR(EAGAIN)) break;
            if (ret < 0) {
                if (ret == AVERROR_EOF) break;
                error = "Receive frame error: " + av_error_string(ret);
                return false;
            }

            // Resample to float32 interleaved.
            int out_samples = swr_get_out_samples(impl_->swr_ctx, impl_->frame->nb_samples);
            std::vector<float> resampled(out_samples * channels);
            uint8_t* out_ptr = (uint8_t*)resampled.data();

            int converted = swr_convert(impl_->swr_ctx, &out_ptr, out_samples,
                (const uint8_t**)impl_->frame->extended_data, impl_->frame->nb_samples);
            if (converted < 0) {
                error = "Resample error: " + av_error_string(converted);
                return false;
            }

            int samples_available = converted * channels;
            int copy = ((needed - filled) < samples_available) ? (needed - filled) : samples_available;
            memcpy(out.data() + filled, resampled.data(), copy * sizeof(float));
            filled += copy;

            // Store residual.
            if (copy < samples_available) {
                impl_->residual.assign(resampled.begin() + copy, resampled.begin() + samples_available);
                impl_->residual_offset = 0;
            }

            if (filled >= needed) break;
        }
    }

    return filled > 0;
}

void AudioReader::close() {
    impl_->cleanup();
    info_ = {};
}

// ---------------------------------------------------------------------------
// AudioWriter
// ---------------------------------------------------------------------------

struct AudioWriter::Impl {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    AVStream* stream = nullptr;
    int64_t pts = 0;
    int channels = 0;
    bool finalized = false;

    ~Impl() { cleanup(); }

    void cleanup() {
        if (packet) { av_packet_free(&packet); packet = nullptr; }
        if (frame) { av_frame_free(&frame); frame = nullptr; }
        if (swr_ctx) { swr_free(&swr_ctx); swr_ctx = nullptr; }
        if (codec_ctx) { avcodec_free_context(&codec_ctx); codec_ctx = nullptr; }
        if (fmt_ctx) {
            if (fmt_ctx->pb) avio_closep(&fmt_ctx->pb);
            avformat_free_context(fmt_ctx);
            fmt_ctx = nullptr;
        }
        stream = nullptr;
        pts = 0;
        channels = 0;
        finalized = false;
    }
};

AudioWriter::AudioWriter() : impl_(std::make_unique<Impl>()) {}
AudioWriter::~AudioWriter() { close(); }

std::string AudioWriter::open(const Config& config) {
    close();

    int ret = avformat_alloc_output_context2(
        &impl_->fmt_ctx, nullptr, nullptr, config.path.c_str());
    if (!impl_->fmt_ctx)
        return "Could not determine output format for '" + config.path + "'";

    const AVCodec* codec = avcodec_find_encoder(impl_->fmt_ctx->oformat->audio_codec);
    if (!codec) {
        // Fall back to AAC.
        codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    }
    if (!codec) {
        close();
        return "No suitable audio encoder found";
    }

    impl_->stream = avformat_new_stream(impl_->fmt_ctx, nullptr);
    if (!impl_->stream) {
        close();
        return "Failed to create audio output stream";
    }

    impl_->codec_ctx = avcodec_alloc_context3(codec);
    if (!impl_->codec_ctx) {
        close();
        return "Failed to allocate audio encoder context";
    }

    impl_->codec_ctx->sample_rate = config.sample_rate;

    // Query supported sample formats.
    const enum AVSampleFormat *supported_fmts = nullptr;
    int num_fmts = 0;
    int qret = avcodec_get_supported_config(impl_->codec_ctx, codec,
        AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
        (const void **)&supported_fmts, &num_fmts);
    if (qret >= 0 && supported_fmts && num_fmts > 0)
        impl_->codec_ctx->sample_fmt = supported_fmts[0];
    else
        impl_->codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    impl_->codec_ctx->time_base = AVRational{1, config.sample_rate};
    impl_->channels = config.channels;

    AVChannelLayout out_layout = {};
    av_channel_layout_default(&out_layout, config.channels);
    av_channel_layout_copy(&impl_->codec_ctx->ch_layout, &out_layout);

    if (impl_->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        impl_->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(impl_->codec_ctx, codec, nullptr);
    if (ret < 0) {
        close();
        return "Failed to open audio encoder: " + av_error_string(ret);
    }

    ret = avcodec_parameters_from_context(impl_->stream->codecpar, impl_->codec_ctx);
    if (ret < 0) {
        close();
        return "Failed to copy audio encoder parameters: " + av_error_string(ret);
    }

    impl_->stream->time_base = impl_->codec_ctx->time_base;

    // Set up swresample: float32 interleaved -> encoder format.
    AVChannelLayout in_layout = {};
    av_channel_layout_default(&in_layout, config.channels);

    ret = swr_alloc_set_opts2(&impl_->swr_ctx,
        &impl_->codec_ctx->ch_layout, impl_->codec_ctx->sample_fmt, config.sample_rate,
        &in_layout, AV_SAMPLE_FMT_FLT, config.sample_rate,
        0, nullptr);
    if (ret < 0) {
        close();
        return "Failed to set up swresample for writer: " + av_error_string(ret);
    }

    ret = swr_init(impl_->swr_ctx);
    if (ret < 0) {
        close();
        return "Failed to init swresample for writer: " + av_error_string(ret);
    }

    if (!(impl_->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&impl_->fmt_ctx->pb, config.path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            close();
            return "Failed to open output file '" + config.path + "': " + av_error_string(ret);
        }
    }

    ret = avformat_write_header(impl_->fmt_ctx, nullptr);
    if (ret < 0) {
        close();
        return "Failed to write header: " + av_error_string(ret);
    }

    impl_->frame = av_frame_alloc();
    impl_->frame->format = impl_->codec_ctx->sample_fmt;
    av_channel_layout_copy(&impl_->frame->ch_layout, &impl_->codec_ctx->ch_layout);
    impl_->frame->sample_rate = config.sample_rate;

    impl_->packet = av_packet_alloc();

    return "";
}

std::string AudioWriter::write_samples(const float* samples, int count_per_channel) {
    if (!impl_->fmt_ctx || !impl_->codec_ctx)
        return "Writer not open";

    int channels = impl_->channels;

    // Allocate a frame for the encoder.
    impl_->frame->nb_samples = count_per_channel;
    int ret = av_frame_get_buffer(impl_->frame, 0);
    if (ret < 0)
        return "Failed to allocate audio frame buffer: " + av_error_string(ret);

    ret = av_frame_make_writable(impl_->frame);
    if (ret < 0)
        return "Audio frame not writable: " + av_error_string(ret);

    // Convert float32 interleaved to encoder format.
    const uint8_t* in_data = (const uint8_t*)samples;
    ret = swr_convert(impl_->swr_ctx, impl_->frame->extended_data, count_per_channel,
        &in_data, count_per_channel);
    if (ret < 0)
        return "Resample error: " + av_error_string(ret);

    impl_->frame->pts = impl_->pts;
    impl_->pts += count_per_channel;

    ret = avcodec_send_frame(impl_->codec_ctx, impl_->frame);
    if (ret < 0)
        return "Send audio frame error: " + av_error_string(ret);

    while (true) {
        ret = avcodec_receive_packet(impl_->codec_ctx, impl_->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0)
            return "Receive audio packet error: " + av_error_string(ret);

        av_packet_rescale_ts(impl_->packet,
            impl_->codec_ctx->time_base, impl_->stream->time_base);
        impl_->packet->stream_index = impl_->stream->index;

        ret = av_interleaved_write_frame(impl_->fmt_ctx, impl_->packet);
        if (ret < 0)
            return "Write audio packet error: " + av_error_string(ret);
    }

    return "";
}

std::string AudioWriter::finalize() {
    if (!impl_->fmt_ctx || !impl_->codec_ctx || impl_->finalized)
        return "";

    impl_->finalized = true;

    int ret = avcodec_send_frame(impl_->codec_ctx, nullptr);
    if (ret < 0 && ret != AVERROR_EOF)
        return "Audio flush error: " + av_error_string(ret);

    while (true) {
        ret = avcodec_receive_packet(impl_->codec_ctx, impl_->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0)
            return "Audio flush receive error: " + av_error_string(ret);

        av_packet_rescale_ts(impl_->packet,
            impl_->codec_ctx->time_base, impl_->stream->time_base);
        impl_->packet->stream_index = impl_->stream->index;

        av_interleaved_write_frame(impl_->fmt_ctx, impl_->packet);
    }

    ret = av_write_trailer(impl_->fmt_ctx);
    if (ret < 0)
        return "Write audio trailer error: " + av_error_string(ret);

    return "";
}

void AudioWriter::close() {
    if (impl_->fmt_ctx && !impl_->finalized)
        finalize();
    impl_->cleanup();
}

} // namespace evx_tools
