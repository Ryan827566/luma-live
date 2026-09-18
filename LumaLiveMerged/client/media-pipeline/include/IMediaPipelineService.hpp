#pragma once

#include "MediaTypes.hpp"
#include "IMediaFrameSink.hpp"
#include <memory>
#include <string>
#include <string_view>

namespace luma::client::media::pipeline {

struct OperationResult { bool success{false}; std::string message; };

class IMediaPipelineService {
public:
    virtual ~IMediaPipelineService() = default;
    virtual OperationResult Start() = 0;
    virtual OperationResult Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual OperationResult Execute(std::string_view operation) = 0;

    // Runtime media ingress. Capture implementations will feed frames into this API.
    virtual OperationResult PushVideoFrame(VideoFrame frame) = 0;
    virtual OperationResult PushAudioFrame(AudioFrame frame) = 0;
    virtual MediaPipelineStats GetStats() const = 0;
    virtual void SetFrameSink(std::shared_ptr<IMediaFrameSink> sink) = 0;
};

} // namespace luma::client::media::pipeline
