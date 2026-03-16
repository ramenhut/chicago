#ifndef EVX_FFMPEG_UTIL_H
#define EVX_FFMPEG_UTIL_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace evx_tools {

struct RGBFrame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int row_pitch() const { return width * 3; }
};

struct VideoInfo {
    int width = 0;
    int height = 0;
    int64_t frame_count = 0;
    double frame_rate = 0.0;
};

// Decodes any FFmpeg-supported file to RGB24 frames.
class VideoReader {
public:
    VideoReader();
    ~VideoReader();
    VideoReader(VideoReader&& other) noexcept;
    VideoReader& operator=(VideoReader&& other) noexcept;

    std::string open(const std::string& path);
    std::string open(const std::string& path, int target_width, int target_height);
    bool read_frame(RGBFrame& frame, std::string& error);
    bool seek_to_start();
    void close();
    const VideoInfo& info() const;

private:
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    VideoInfo info_;
};

// Writes RGB24 frames to any FFmpeg-supported format.
class VideoWriter {
public:
    VideoWriter();
    ~VideoWriter();

    struct Config {
        std::string path;
        int width = 0;
        int height = 0;
        double frame_rate = 30.0;
    };

    std::string open(const Config& config);
    std::string write_frame(const RGBFrame& frame);
    std::string finalize();
    void close();

private:
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string av_error_string(int errnum);

// Audio data types.
struct AudioInfo {
    int sample_rate = 0;
    int channels = 0;
    int64_t total_samples = 0;     // total samples per channel, 0 if unknown
};

// Decodes any FFmpeg-supported file's audio to float32 interleaved PCM.
class AudioReader {
public:
    AudioReader();
    ~AudioReader();
    AudioReader(AudioReader&& other) noexcept;
    AudioReader& operator=(AudioReader&& other) noexcept;

    std::string open(const std::string& path, int target_sample_rate = 0, int target_channels = 0);
    bool read_samples(std::vector<float>& samples, int frame_size, std::string& error);
    void close();
    const AudioInfo& info() const;

private:
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioInfo info_;
};

// Writes float32 interleaved PCM to any FFmpeg-supported audio format.
class AudioWriter {
public:
    AudioWriter();
    ~AudioWriter();

    struct Config {
        std::string path;
        int sample_rate = 44100;
        int channels = 1;
    };

    std::string open(const Config& config);
    std::string write_samples(const float* samples, int count_per_channel);
    std::string finalize();
    void close();

private:
    AudioWriter(const AudioWriter&) = delete;
    AudioWriter& operator=(const AudioWriter&) = delete;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace evx_tools

#endif // EVX_FFMPEG_UTIL_H
