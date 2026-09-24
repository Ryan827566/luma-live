#pragma once
#include "MediaTypes.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace luma::client::media {
// Captures the primary display only after an explicit Start from the UI.
class ScreenCaptureService {
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex errorMutex_;
    std::string error_;
public:
    ~ScreenCaptureService() { Stop(); }
    bool Start(std::function<void(pipeline::VideoFrame)> callback);
    void Stop();
    bool IsCapturing() const { return running_; }
    std::string LastError() const { std::lock_guard lock(errorMutex_); return error_; }
};
}
