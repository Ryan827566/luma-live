#pragma once
#include "PeerConnectionConfig.hpp"
#include "MediaTypes.hpp"
#include "IMediaFrameSink.hpp"
#include <functional>
#include <memory>
#include <string>

namespace luma::client::webrtc {

struct WebRtcNetworkStats {
    bool report_ready{false};
    bool rtt_ready{false};
    bool inbound_ready{false};
    bool jitter_ready{false};
    double round_trip_time_ms{0};
    double jitter_ms{0}; // Maximum jitter across inbound RTP streams.
    std::uint64_t packets_received{0};
    std::int64_t packets_lost{0}; // Signed cumulative RFC 3550 counter.
    double packet_loss_percent{0}; // Cumulative, not an interval estimate.
    std::string selected_candidate_pair_id;
};

struct WebRtcCallbacks {
    std::function<void(const std::string& type, const std::string& sdp)> on_local_description;
    std::function<void(const std::string& mid, int mline_index, const std::string& candidate)> on_local_ice_candidate;
    std::function<void(const std::string& state)> on_connection_state;
    std::function<void()> on_remote_video_frame;
    std::function<void()> on_remote_audio_frame;
    // Invoked on WebRTC threads; UI callers must marshal owned frames to their UI thread.
    std::function<void(media::pipeline::VideoFrame)> on_remote_video;
    std::function<void(media::pipeline::AudioFrame)> on_remote_audio;
    std::function<void()> on_remote_description_set;
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
    bool CreateOffer(bool ice_restart = false);
    bool CreateAnswer();
    bool SetRemoteDescription(const std::string& type, const std::string& sdp);
    bool AddRemoteIceCandidate(const std::string& mid, int mline_index, const std::string& candidate);
    // Call from the peer's owning thread, like Close(). Callback runs on a
    // WebRTC thread and owns its result. False means no request was submitted.
    // Close() cancels pending callbacks; UI consumers must still marshal to UI.
    bool GetNetworkStats(std::function<void(WebRtcNetworkStats)> callback);
    void Close();
    bool IsInitialized() const noexcept;
private:
    NativeWebRtcPeerConnection();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace luma::client::webrtc
