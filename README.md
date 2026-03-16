# Chicago Codec (EVX3)

The [Cairo](http://www.bertolami.com/index.php?engine=portfolio&content=compression&detail=cairo-video-codec) low-latency video codec was originally created in 2011 for the [everyAir](http://www.everyair.net) mobile cloud gaming project, enabling real-time game streaming from PC, Mac, and PS3 to early iOS devices (original iPad, iPhone 3GS). Chicago is an evolution of Cairo that adds:

- B-frame bidirectional prediction with hierarchical references
- Sample Adaptive Offset (SAO) filter with Band Offset and Edge Offset modes
- Trellis quantization for RD-optimal coefficient optimization
- Significance-map coefficient coding with per-position entropy contexts
- Wavefront Parallel Processing (WPP) for multithreaded encode/decode
- MDCT-based audio codec with psychoacoustic quantization and stereo support
- Audio/video multiplexer with interleaved streaming and A/V sync
- Resolution support up to 64K

While Cairo achieved roughly 35:1 compression, Chicago improves this to approximately 75:1 for equivalent quality.

Demo: [PS3 streaming](https://youtu.be/B14c8gFgdXM?t=64) | [latency test](https://youtu.be/IN4wC_SVaN8?t=19) | [everyAir](https://www.youtube.com/watch?v=amMRNjE6MsQ)

## Features

**Video**
- Intra (I-frame) and inter (P-frame) prediction with multiple reference frames
- Half and quarter pixel motion compensation with 6-tap Wiener interpolation
- 8x8 DCT transform with MPEG-2 style quantization matrices
- Variance-adaptive quantization with trellis optimization
- RDO-driven mode selection (intra mode, motion vectors, copy/skip)
- In-loop deblocking filter with QP-adaptive strength
- Sample Adaptive Offset (SAO) filter with Band Offset and Edge Offset modes
- Significance-map coefficient coding with per-position entropy contexts
- Wavefront Parallel Processing (WPP) for multithreaded encode/decode
- Adaptive binary arithmetic coding with Exponential Golomb precoding

**Audio**
- MDCT-based transform codec with configurable frame size
- Psychoacoustic quantization with quality control
- Stereo support with independent channel coding

**Container**
- Multiplexer supporting interleaved audio/video streams
- Timestamp preservation for A/V synchronization

## Building

Requirements: CMake 3.16+, C++17 compiler. CLI tools require FFmpeg; the player requires SDL2.

```bash
# Configure and build
cmake -B build
cmake --build build

# Run tests
cd build && ctest

# Install dependencies (macOS)
brew install ffmpeg sdl2
```

Build options:
- `CHICAGO_BUILD_TOOLS=ON` — Build CLI tools (default ON, requires FFmpeg)
- `CHICAGO_BUILD_TESTS=ON` — Build test suite (default ON, fetches Google Test)
- `CHICAGO_USE_VMAF=OFF` — Enable VMAF metric in analyzer (requires libvmaf)

## CLI Tools

```bash
# Encode video (quality 1-31, lower = better)
build/tools/evx_encode input.mp4 -o output.evx -q 8

# Decode video
build/tools/evx_decode output.evx -o output.mp4

# Analyze encode quality (PSNR, bitrate curves)
build/tools/evx_analyze input.mp4 -q 8

# Play back directly
build/tools/evx_play output.evx

# Audio-only encode/decode
build/tools/evx_audio_encode input.wav -o output.evx -q 4
build/tools/evx_audio_decode output.evx -o output.wav

# Muxed audio+video encode/decode (default behavior of evx_encode/evx_decode)
```

## Quality Settings

Quality ranges from 1 (best) to 31 (most compressed). Levels 1-16 map linearly to the internal quantization parameter. Above 16, a quadratic curve increases compression more aggressively:

| Quality | Approximate Use Case |
|---------|---------------------|
| 1-4     | Near-lossless, archival |
| 5-8     | High quality, general use |
| 9-16    | Good quality, moderate compression |
| 17-24   | Moderate quality, smaller files |
| 25-31   | Low quality, maximum compression |

## Patent Notice

While this release is provided under an open and permissive copyright license, the algorithms it contains are likely to be covered by existing patents that may restrict your ability to use this codec commercially.

## More Information

For more information visit [bertolami.com](http://www.bertolami.com/index.php?engine=portfolio&content=compression&detail=cairo-video-codec).
