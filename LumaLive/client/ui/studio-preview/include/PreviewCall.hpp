#pragma once
#include "NativeWebRtcPeerConnection.hpp"
#include "TcpSignalingClient.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <vector>
#include <utility>

namespace luma::client::ui::preview {
enum class CallState { Offline, Joining, Ready, Outgoing, Incoming, Connecting, Connected };

// Control methods and accessors belong to the UI thread. Video/Audio may run on
// capture threads. Joining only discovers participants; media requires consent.
class PreviewCall {
    using Clock = std::chrono::steady_clock;
    using T = contracts::SignalingMessageType;
    struct Event {
        int kind{};
        contracts::SignalingMessage signal;
        std::string type, text;
        std::uint64_t generation{};
    };
    std::mutex mutex_, mediaMutex_;
    std::deque<Event> events_;
    std::shared_ptr<webrtc::NativeWebRtcPeerConnection> rtc_;
    signaling::TcpSignalingClient signaling_;
    webrtc::WebRtcCallbacks callbacks_;
    contracts::PeerConnectionConfig config_;
    std::string room_, peer_, remote_, status_;
    std::vector<std::string> participants_;
    std::deque<std::pair<std::string, std::int64_t>> seenInvites_;
    std::vector<contracts::SignalingMessage> pendingIce_;
    std::atomic<bool> accepting_{false}, mediaReady_{false};
    std::atomic<std::uint64_t> generation_{0};
    std::uint64_t registration_{0};
    std::int64_t callId_{0};
    bool remoteSet_{false}, answerPending_{false}, caller_{false};
    CallState state_{CallState::Offline};
    std::chrono::milliseconds timeout_;
    Clock::time_point deadline_{}, started_{};

    void Queue(Event e) {
        std::lock_guard lock(mutex_);
        if (accepting_ && events_.size() < 512) events_.push_back(std::move(e));
    }
    bool Send(contracts::SignalingMessage m) {
        m.room_id = room_; m.peer_id = peer_;
        return signaling_.Send(m);
    }
    bool SendCall(T type) {
        contracts::SignalingMessage m;
        m.type = type; m.target_peer_id = remote_; m.sequence = callId_;
        return Send(std::move(m));
    }
    void CloseMedia() {
        mediaReady_ = false;
        ++generation_;
        std::lock_guard lock(mediaMutex_);
        if (rtc_) rtc_->Close();
        rtc_.reset();
        pendingIce_.clear(); remoteSet_ = false; answerPending_ = false;
    }
    void End(const std::string& reason) {
        CloseMedia();
        remote_.clear(); callId_ = 0; started_ = {}; caller_ = false;
        state_ = accepting_ && signaling_.IsConnected() ? CallState::Ready : CallState::Offline;
        status_ = reason;
    }
    bool OpenMedia() {
        CloseMedia();
        const auto generation = generation_.load();
        auto cb = callbacks_;
        cb.on_local_description = [this, generation](const auto& type, const auto& sdp) {
            Queue({1, {}, type, sdp, generation});
        };
        cb.on_local_ice_candidate = [this, generation](const auto& mid, int line, const auto& candidate) {
            contracts::SignalingMessage m;
            m.type = T::IceCandidate; m.candidate_mid = mid;
            m.value = std::to_string(line); m.candidate = candidate;
            Queue({2, std::move(m), {}, {}, generation});
        };
        cb.on_remote_description_set = [this, generation] { Queue({3, {}, {}, {}, generation}); };
        cb.on_connection_state = [this, generation](const auto& state) { Queue({4, {}, {}, state, generation}); };
        auto video = cb.on_remote_video;
        cb.on_remote_video = [this, generation, video](auto frame) {
            if (generation == generation_.load() && mediaReady_ && video) video(std::move(frame));
        };
        auto audio = cb.on_remote_audio;
        cb.on_remote_audio = [this, generation, audio](auto frame) {
            if (generation == generation_.load() && mediaReady_ && audio) audio(std::move(frame));
        };
        std::lock_guard lock(mediaMutex_);
        rtc_ = webrtc::NativeWebRtcPeerConnection::Create();
        if (!rtc_->Initialize(config_, std::move(cb))) {
            rtc_->Close(); rtc_.reset(); return false;
        }
        mediaReady_ = true;
        return true;
    }
    bool AddIce(const contracts::SignalingMessage& m) {
        try {
            std::size_t used = 0;
            int line = std::stoi(m.value, &used);
            return used == m.value.size() && line >= 0 && rtc_ &&
                rtc_->AddRemoteIceCandidate(m.candidate_mid, line, m.candidate);
        } catch (...) { return false; }
    }
    void Fail(const std::string& reason) { SendCall(T::CallHangup); End(reason); }
public:
    explicit PreviewCall(std::chrono::milliseconds timeout = std::chrono::seconds(30))
        : timeout_(std::max(timeout, std::chrono::milliseconds(1))) {}
    ~PreviewCall() { Stop(); }
    bool Active() const { return accepting_; }
    CallState State() const { return state_; }
    const std::vector<std::string>& Participants() const { return participants_; }
    const std::string& Remote() const { return remote_; }
    const std::string& LastStatus() const { return status_; }
    std::int64_t DurationSeconds() const {
        return started_ == Clock::time_point{} ? 0 :
            std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started_).count();
    }
    bool Start(const std::string& host, uint16_t port, const std::string& room,
               const std::string& peer, webrtc::WebRtcCallbacks cb) {
        contracts::PeerConnectionConfig config;
        // Preserve deployment configuration from the existing client. Tests can
        // pass an explicit empty config to run with local candidates only.
        const char* stun = std::getenv("LUMALIVE_STUN_SERVER");
        config.stun_servers.push_back(stun && *stun ? stun : "stun:stun.l.google.com:19302");
        const char* turn = std::getenv("LUMALIVE_TURN_URL");
        const char* user = std::getenv("LUMALIVE_TURN_USERNAME");
        const char* password = std::getenv("LUMALIVE_TURN_PASSWORD");
        if (turn && *turn) {
            config.turn_url = turn; config.turn_username = user ? user : "";
            config.turn_password = password ? password : "";
        }
        return Start(host, port, room, peer, std::move(cb), config);
    }
    bool Start(const std::string& host, uint16_t port, const std::string& room,
               const std::string& peer, webrtc::WebRtcCallbacks cb,
               const contracts::PeerConnectionConfig& config) {
        Stop();
        if (host.empty() || !port || room.empty() || peer.empty()) {
            status_ = "Server, room and participant ID are required"; return false;
        }
        room_ = room; peer_ = peer; callbacks_ = std::move(cb); config_ = config;
        accepting_ = true;
        const auto registration = ++registration_;
        if (!signaling_.Connect(host, port, [this, registration](const auto& m) {
            Queue({0, m, {}, {}, registration});
        })) { Stop(); status_ = "Signaling connection failed"; return false; }
        contracts::SignalingMessage join; join.type = T::JoinRoom;
        if (!Send(join)) { Stop(); status_ = "Unable to join room"; return false; }
        state_ = CallState::Joining; deadline_ = Clock::now() + std::min(timeout_, std::chrono::milliseconds(3000));
        status_ = "Joining room"; return true;
    }
    bool Call(const std::string& target) {
        if (state_ != CallState::Ready || target == peer_ ||
            std::find(participants_.begin(), participants_.end(), target) == participants_.end()) return false;
        static std::atomic<std::int64_t> nextId{
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()};
        callId_ = ++nextId; remote_ = target; caller_ = true;
        state_ = CallState::Outgoing; deadline_ = Clock::now() + timeout_;
        status_ = "Calling " + remote_;
        if (!SendCall(T::CallInvite)) { End("Unable to send invitation"); return false; }
        return true;
    }
    bool Accept() {
        if (state_ != CallState::Incoming) return false;
        if (!OpenMedia()) { Fail("Media initialization failed"); return false; }
        state_ = CallState::Connecting; deadline_ = Clock::now() + timeout_;
        status_ = "Connecting to " + remote_;
        if (!SendCall(T::CallAccept)) { End("Unable to accept invitation"); return false; }
        return true;
    }
    bool Reject() {
        if (state_ != CallState::Incoming) return false;
        const bool sent = SendCall(T::CallReject); End("Call rejected"); return sent;
    }
    bool Hangup() {
        if (remote_.empty()) return false;
        const auto type = state_ == CallState::Outgoing ? T::CallCancel :
            state_ == CallState::Incoming ? T::CallReject : T::CallHangup;
        const bool sent = SendCall(type); End("Call ended"); return sent;
    }
    void Video(const media::pipeline::VideoFrame& frame) {
        std::lock_guard lock(mediaMutex_);
        if (rtc_ && mediaReady_) rtc_->AddVideoFrame(frame);
    }
    void Audio(const media::pipeline::AudioFrame& frame) {
        std::lock_guard lock(mediaMutex_);
        if (rtc_ && mediaReady_) rtc_->AddAudioFrame(frame);
    }
    std::string Poll() {
        const auto previous = status_;
        std::deque<Event> work;
        { std::lock_guard lock(mutex_); work.swap(events_); }
        for (auto& e : work) {
            if (e.kind != 0) {
                if (e.generation != generation_ || !rtc_ || remote_.empty()) continue;
                if (e.kind == 4) {
                    if (e.text == "connected") {
                        state_ = CallState::Connected;
                        if (started_ == Clock::time_point{}) started_ = Clock::now();
                        status_ = "Connected to " + remote_;
                    } else if (e.text == "disconnected") {
                        state_ = CallState::Connecting; deadline_ = Clock::now() + timeout_;
                        status_ = "Connection interrupted; attempting recovery";
                    } else if (e.text == "failed" || e.text == "closed" ||
                               e.text.find("error") != std::string::npos) Fail("Media connection failed: " + e.text);
                    continue;
                }
                if (e.kind == 1 || e.kind == 2) {
                    auto m = e.signal;
                    if (e.kind == 1) { m.type = e.type == "offer" ? T::Offer : T::Answer; m.sdp = e.text; }
                    m.target_peer_id = remote_; m.sequence = callId_;
                    if (!Send(std::move(m))) End("Signaling send failed");
                    continue;
                }
                if (e.kind == 3) {
                    remoteSet_ = true;
                    bool valid = true;
                    for (const auto& m : pendingIce_) valid = AddIce(m) && valid;
                    pendingIce_.clear();
                    if (!valid) { Fail("Invalid remote network candidate"); continue; }
                    if (answerPending_) {
                        answerPending_ = false;
                        if (!rtc_->CreateAnswer()) Fail("Unable to create answer");
                    }
                }
                continue;
            }
            if (e.generation != registration_) continue;
            const auto& m = e.signal;
            if (m.room_id != room_) continue;
            if (m.type == T::RoomJoined && m.target_peer_id == peer_ && state_ == CallState::Joining) {
                state_ = CallState::Ready; status_ = "Ready; choose a participant to call"; continue;
            }
            if (m.type == T::Error && m.target_peer_id == peer_) {
                if (state_ == CallState::Joining) { Stop(); status_ = m.value; }
                else if (m.sequence == callId_ && callId_ != 0) End(m.value);
                continue;
            }
            if (m.peer_id == peer_ || m.peer_id.empty()) continue;
            if (m.type == T::PeerJoined) {
                if (std::find(participants_.begin(), participants_.end(), m.peer_id) == participants_.end()) {
                    participants_.push_back(m.peer_id); std::sort(participants_.begin(), participants_.end());
                }
                continue;
            }
            if (m.type == T::PeerLeft) {
                participants_.erase(std::remove(participants_.begin(), participants_.end(), m.peer_id), participants_.end());
                if (m.peer_id == remote_) End("Remote participant left the room");
                continue;
            }
            if (m.target_peer_id != peer_ || m.sequence <= 0) continue;
            if (m.type == T::CallInvite) {
                const auto key = std::make_pair(m.peer_id, m.sequence);
                if (std::find(seenInvites_.begin(), seenInvites_.end(), key) != seenInvites_.end()) continue;
                seenInvites_.push_back(key);
                if (seenInvites_.size() > 128) seenInvites_.pop_front();
                if (state_ == CallState::Ready) {
                    remote_ = m.peer_id; callId_ = m.sequence; caller_ = false;
                    state_ = CallState::Incoming; deadline_ = Clock::now() + timeout_;
                    status_ = "Incoming call from " + remote_;
                } else if (m.peer_id != remote_ || m.sequence != callId_) {
                    contracts::SignalingMessage busy; busy.type = T::CallBusy;
                    busy.target_peer_id = m.peer_id; busy.sequence = m.sequence; Send(busy);
                }
                continue;
            }
            if (m.peer_id != remote_ || m.sequence != callId_) continue;
            switch (m.type) {
            case T::CallAccept:
                if (state_ == CallState::Outgoing && caller_) {
                    if (!OpenMedia()) { Fail("Media initialization failed"); break; }
                    state_ = CallState::Connecting; deadline_ = Clock::now() + timeout_;
                    status_ = "Connecting to " + remote_;
                    if (!rtc_->CreateOffer()) Fail("Unable to create offer");
                }
                break;
            case T::CallReject: if (state_ == CallState::Outgoing) End("Call rejected by remote participant"); break;
            case T::CallBusy: if (state_ == CallState::Outgoing) End("Remote participant is busy"); break;
            case T::CallCancel: if (state_ == CallState::Incoming || state_ == CallState::Connecting) End("Caller cancelled"); break;
            case T::CallHangup: End("Remote participant ended the call"); break;
            case T::Offer:
                if (!caller_ && state_ == CallState::Connecting && rtc_ && !answerPending_ && !remoteSet_) {
                    answerPending_ = true;
                    if (!rtc_->SetRemoteDescription("offer", m.sdp)) Fail("Invalid remote offer");
                }
                break;
            case T::Answer:
                if (caller_ && state_ == CallState::Connecting && rtc_ && !remoteSet_)
                    if (!rtc_->SetRemoteDescription("answer", m.sdp)) Fail("Invalid remote answer");
                break;
            case T::IceCandidate:
                if (!rtc_) break;
                if (!remoteSet_) {
                    if (pendingIce_.size() < 128) pendingIce_.push_back(m);
                    else Fail("Too many remote network candidates");
                } else if (!AddIce(m)) Fail("Invalid remote network candidate");
                break;
            default: break;
            }
        }
        if (Active() && !signaling_.IsConnected()) { Stop(); status_ = "Signaling disconnected; rejoin to reconnect"; }
        if ((state_ == CallState::Joining || state_ == CallState::Incoming ||
             state_ == CallState::Outgoing || state_ == CallState::Connecting) && Clock::now() >= deadline_) {
            if (state_ == CallState::Joining) { Stop(); status_ = "Room registration timed out"; }
            else { Hangup(); status_ = "Call timed out"; }
        }
        return previous == status_ ? std::string{} : status_;
    }
    void Stop() {
        if (accepting_ && signaling_.IsConnected()) {
            if (!remote_.empty()) Hangup();
            contracts::SignalingMessage leave; leave.type = T::LeaveRoom; Send(leave);
        }
        accepting_ = false;
        signaling_.Close(); CloseMedia(); ++registration_;
        { std::lock_guard lock(mutex_); events_.clear(); }
        participants_.clear(); seenInvites_.clear(); remote_.clear(); callId_ = 0; started_ = {};
        state_ = CallState::Offline; status_ = "Offline";
    }
};
}



