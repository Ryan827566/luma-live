#pragma once
#include "CaptureTypes.hpp"
#include "DeviceCaptureFactory.hpp"
#include "MediaPipelineFactory.hpp"
#include "NativeWebRtcPeerConnection.hpp"
#include "TcpSignalingClient.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace luma::client::live {

struct LivePublishConfig {
    std::string signaling_host{"127.0.0.1"};
    std::uint16_t signaling_port{9000};
    std::string room_id;
    std::string peer_id;
    luma::contracts::PeerConnectionConfig rtc;
    luma::client::media::CameraCaptureConfig camera;
    luma::client::media::AudioCaptureConfig audio;
};

class LivePublishSession final {
public:
    LivePublishSession();
    ~LivePublishSession();
    bool Start(const LivePublishConfig& config);
    void Stop();
    bool IsRunning() const noexcept { return running_.load(); }
    std::string LastError() const;
private:
    void HandleSignal(const luma::contracts::SignalingMessage& message);
    void Cleanup();
    std::unique_ptr<luma::client::media::IDeviceCaptureService> capture_;
    std::unique_ptr<luma::client::media::pipeline::IMediaPipelineService> pipeline_;
    std::shared_ptr<luma::client::webrtc::NativeWebRtcPeerConnection> rtc_;
    std::unique_ptr<luma::client::signaling::TcpSignalingClient> signaling_;
    std::string room_id_;
    std::string peer_id_;
    std::string remote_peer_id_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::string last_error_;
};

} // namespace luma::client::live
