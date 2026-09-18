#pragma once
#include "MediaTypes.hpp"
namespace luma::client::media::pipeline {
class IMediaFrameSink {
public:
    virtual ~IMediaFrameSink() = default;
    virtual void OnVideoFrame(VideoFrame frame) = 0;
    virtual void OnAudioFrame(AudioFrame frame) = 0;
};
}
