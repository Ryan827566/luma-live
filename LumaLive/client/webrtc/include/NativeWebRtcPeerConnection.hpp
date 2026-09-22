#pragma once
#include "PeerConnectionConfig.hpp"
#include "MediaTypes.hpp"
#include "IMediaFrameSink.hpp"
#include <functional>
#include <memory>
#include <string>

namespace luma::client::webrtc {

struct WebRtcCallbacks {
    std::function<void(const std::string& type, const std::string& sdp)> on_local_description;
    std::function<void(const std::string& mid, int mline_index, const std::string& candidate)> on_local_ice_candidate;
    std::function<void(const std::string& state)> on_connection_state;
    std::function<void()> on_remote_video_frame;
    std::function<void()> on_remote_audio_frame;
};

class NativeWebRtcPeerConnection : public luma::client::media::pipeline::IMediaFrameSink, public std::enable_shared_from_this<NativeWebRtcPeerConnection> {
public:
    static std::shared_ptr<NativeWebRtcPeerConnection> Create();
    ~NativeWebRtcPeerConnection();
    bool Initialize(const luma::contracts::PeerConnectionConfig& config, WebRtcCallbacks callbacks);
    void OnVideoFrame(luma::client::media::pipeline::VideoFrame frame) override { (void)AddVideoFrame(frame); }
    void OnAudioFrame(luma::client::media::pipeline::AudioFrame frame) override { (void)AddAudioFrame(frame); }
    bool AddVideoFrame(const luma::client::media::pipeline::VideoFrame& frame);
    bool AddAudioFrame(const luma::client::media::pipeline::AudioFrame& frame);
    // Returns true when the asynchronous operation was successfully submitted.
    // SDP success/failure is reported through WebRtcCallbacks::on_local_description
    // and WebRtcCallbacks::on_connection_state respectively.
    bool CreateOffer();
    bool CreateAnswer();
    bool SetRemoteDescription(const std::string& type, const std::string& sdp);
    bool AddRemoteIceCandidate(const std::string& mid, int mline_index, const std::string& candidate);
    void Close();
    bool IsInitialized() const noexcept;
private:
    NativeWebRtcPeerConnection();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace luma::client::webrtc
