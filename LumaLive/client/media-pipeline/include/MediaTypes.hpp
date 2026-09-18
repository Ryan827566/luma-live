#pragma once

#include <cstdint>
#include <vector>

namespace luma::client::media::pipeline {

enum class PixelFormat { I420, NV12, BGRA, RGBA };
enum class AudioSampleFormat { S16, S32, Float32 };

struct VideoFrame {
    std::uint64_t timestamp_us{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    PixelFormat format{PixelFormat::I420};
    std::vector<std::uint8_t> data;

    bool IsValid() const noexcept {
        return width > 0 && height > 0 && !data.empty();
    }
};

struct AudioFrame {
    std::uint64_t timestamp_us{0};
    std::uint32_t sample_rate{48000};
    std::uint16_t channels{2};
    AudioSampleFormat format{AudioSampleFormat::Float32};
    std::vector<std::uint8_t> data;

    bool IsValid() const noexcept {
        if (sample_rate == 0 || channels == 0 || data.empty()) return false;

        std::size_t bytes_per_sample = 0;
        switch (format) {
        case AudioSampleFormat::S16: bytes_per_sample = sizeof(std::int16_t); break;
        case AudioSampleFormat::S32: bytes_per_sample = sizeof(std::int32_t); break;
        case AudioSampleFormat::Float32: bytes_per_sample = sizeof(float); break;
        default: return false;
        }

        const std::size_t bytes_per_frame = bytes_per_sample * channels;
        return bytes_per_frame != 0 && data.size() % bytes_per_frame == 0;
    }
};

struct MediaPipelineStats {
    std::uint64_t video_frames{0};
    std::uint64_t audio_frames{0};
    std::uint64_t dropped_video_frames{0};
    std::uint64_t dropped_audio_frames{0};
};

} // namespace luma::client::media::pipeline
