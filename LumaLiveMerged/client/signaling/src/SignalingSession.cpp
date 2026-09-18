#include "SignalingSession.hpp"

namespace luma::client::signaling {

bool SignalingSession::BeginConnect() {
    if (state_ != SignalingState::Disconnected) return false;
    state_ = SignalingState::Connecting;
    return true;
}

bool SignalingSession::MarkConnected() {
    if (state_ != SignalingState::Connecting) return false;
    state_ = SignalingState::Connected;
    return true;
}

bool SignalingSession::JoinRoom(std::string room_id, std::string peer_id) {
    if (state_ != SignalingState::Connected || room_id.empty() || peer_id.empty()) return false;
    room_id_ = std::move(room_id);
    peer_id_ = std::move(peer_id);
    state_ = SignalingState::InRoom;
    return true;
}

bool SignalingSession::LeaveRoom() {
    if (state_ != SignalingState::InRoom) return false;
    room_id_.clear();
    state_ = SignalingState::Connected;
    return true;
}

void SignalingSession::Disconnect() {
    room_id_.clear();
    peer_id_.clear();
    inbox_.clear();
    state_ = SignalingState::Disconnected;
}

bool SignalingSession::Handle(const luma::contracts::SignalingMessage& message) {
    if (message.type == luma::contracts::SignalingMessageType::Error) return false;
    if (state_ != SignalingState::InRoom) return false;
    if (!message.room_id.empty() && message.room_id != room_id_) return false;
    if (message.peer_id == peer_id_) return false;
    inbox_.push_back(message);
    return true;
}

} // namespace luma::client::signaling
