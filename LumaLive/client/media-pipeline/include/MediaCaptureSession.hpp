#pragma once
#include "CaptureTypes.hpp"
#include "DeviceCaptureFactory.hpp"
#include "MediaPipelineFactory.hpp"
#include <memory>

namespace luma::client::media {

// Owns the capture-to-pipeline connection. This is deliberately small so the
// WebRTC layer can later consume the same pipeline without knowing Windows APIs.
class MediaCaptureSession final {
public:
    MediaCaptureSession(std::unique_ptr<IDeviceCaptureService> capture,
                        std::unique_ptr<pipeline::IMediaPipelineService> pipeline);
    ~MediaCaptureSession();

    shared::contracts::Result Start(const CameraCaptureConfig& camera,
                                    const AudioCaptureConfig& audio);
    shared::contracts::Result Stop();
    bool IsRunning() const noexcept { return running_; }
    pipeline::MediaPipelineStats GetStats() const { return pipeline_->GetStats(); }

private:
    std::unique_ptr<IDeviceCaptureService> capture_;
    std::unique_ptr<pipeline::IMediaPipelineService> pipeline_;
    bool running_{false};
};

}
