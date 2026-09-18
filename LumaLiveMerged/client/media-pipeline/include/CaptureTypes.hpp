#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "MediaTypes.hpp"

namespace luma::client::media {

enum class CaptureDeviceType { Camera, Microphone };

struct CaptureDeviceInfo {
    std::string id;
    std::string name;
    CaptureDeviceType type{CaptureDeviceType::Camera};
};

struct CaptureDeviceList {
    std::vector<CaptureDeviceInfo> devices;
};

struct CameraCaptureConfig {
    std::string device_id;
    std::uint32_t width{1280};
    std::uint32_t height{720};
    std::uint32_t fps{30};
};

struct AudioCaptureConfig {
    std::string device_id;
    std::uint32_t sample_rate{48000};
    std::uint16_t channels{2};
    pipeline::AudioSampleFormat format{pipeline::AudioSampleFormat::S16};
};

using VideoFrameCallback = std::function<void(pipeline::VideoFrame)>;
using AudioFrameCallback = std::function<void(pipeline::AudioFrame)>;

}
