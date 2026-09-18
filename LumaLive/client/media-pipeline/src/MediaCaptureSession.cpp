#include "MediaCaptureSession.hpp"
#include <utility>

namespace luma::client::media {

MediaCaptureSession::MediaCaptureSession(std::unique_ptr<IDeviceCaptureService> capture,
                                         std::unique_ptr<pipeline::IMediaPipelineService> pipeline)
    : capture_(std::move(capture)), pipeline_(std::move(pipeline)) {}

MediaCaptureSession::~MediaCaptureSession() { Stop(); }

shared::contracts::Result MediaCaptureSession::Start(const CameraCaptureConfig& camera,
                                                     const AudioCaptureConfig& audio) {
    if (running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState, "media capture session already running");
    auto capture_result = capture_->Start();
    if (!capture_result.IsOk()) return capture_result;
    auto pipeline_result = pipeline_->Start();
    if (!pipeline_result.success) {
        capture_->Stop();
        return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal, pipeline_result.message);
    }

    capture_result = capture_->StartCamera(camera, [this](pipeline::VideoFrame frame) {
        (void)pipeline_->PushVideoFrame(std::move(frame));
    });
    if (!capture_result.IsOk()) {
        pipeline_->Stop(); capture_->Stop(); return capture_result;
    }
    capture_result = capture_->StartMicrophone(audio, [this](pipeline::AudioFrame frame) {
        (void)pipeline_->PushAudioFrame(std::move(frame));
    });
    if (!capture_result.IsOk()) {
        capture_->StopCamera(); pipeline_->Stop(); capture_->Stop(); return capture_result;
    }
    running_ = true;
    return shared::contracts::Result::Ok();
}

shared::contracts::Result MediaCaptureSession::Stop() {
    if (!running_) return shared::contracts::Result::Ok();
    capture_->StopCamera();
    capture_->StopMicrophone();
    pipeline_->Stop();
    capture_->Stop();
    running_ = false;
    return shared::contracts::Result::Ok();
}

}
