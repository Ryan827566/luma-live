#include "IMediaPipelineService.hpp"
#include "MediaPipelineFactory.hpp"
#include <memory>
#include <utility>
#include <mutex>

namespace luma::client::media::pipeline {

class MediaPipelineService final : public IMediaPipelineService {
public:
    OperationResult Start() override {
        std::lock_guard lock(mutex_);
        if (running_) return {false, "media pipeline is already running"};
        running_ = true;
        return {true, "media pipeline started"};
    }

    OperationResult Stop() override {
        std::lock_guard lock(mutex_);
        if (!running_) return {false, "media pipeline is not running"};
        running_ = false;
        return {true, "media pipeline stopped"};
    }

    bool IsRunning() const override { std::lock_guard lock(mutex_); return running_; }

    OperationResult Execute(std::string_view operation) override {
        if (operation.empty()) return {false, "operation is empty"};
        if (operation == "start") return Start();
        if (operation == "stop") return Stop();
        if (operation == "reset_stats") {
            std::lock_guard lock(mutex_);
            stats_ = {};
            return {true, "media pipeline statistics reset"};
        }
        return {false, "unsupported media pipeline operation: " + std::string(operation)};
    }

    OperationResult PushVideoFrame(VideoFrame frame) override {
        std::unique_lock lock(mutex_);
        if (!running_) return {false, "media pipeline is not running"};
        if (!frame.IsValid()) {
            ++stats_.dropped_video_frames;
            return {false, "invalid video frame"};
        }
        ++stats_.video_frames;
        last_video_timestamp_us_ = frame.timestamp_us;
        auto sink = sink_;
        lock.unlock();
        if (sink) sink->OnVideoFrame(std::move(frame));
        return {true, "video frame accepted"};
    }

    OperationResult PushAudioFrame(AudioFrame frame) override {
        std::unique_lock lock(mutex_);
        if (!running_) return {false, "media pipeline is not running"};
        if (!frame.IsValid()) {
            ++stats_.dropped_audio_frames;
            return {false, "invalid audio frame"};
        }
        ++stats_.audio_frames;
        last_audio_timestamp_us_ = frame.timestamp_us;
        auto sink = sink_;
        lock.unlock();
        if (sink) sink->OnAudioFrame(std::move(frame));
        return {true, "audio frame accepted"};
    }

    void SetFrameSink(std::shared_ptr<IMediaFrameSink> sink) override { std::lock_guard lock(mutex_); sink_ = std::move(sink); }

    MediaPipelineStats GetStats() const override {
        std::lock_guard lock(mutex_);
        return stats_;
    }

private:
    bool running_{false};
    mutable std::mutex mutex_;
    MediaPipelineStats stats_{};
    std::shared_ptr<IMediaFrameSink> sink_;
    std::uint64_t last_video_timestamp_us_{0};
    std::uint64_t last_audio_timestamp_us_{0};
};

std::unique_ptr<IMediaPipelineService> CreateMediaPipelineService() {
    return std::make_unique<MediaPipelineService>();
}

} // namespace luma::client::media::pipeline
