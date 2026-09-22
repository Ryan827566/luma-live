#pragma once
#include "SignalingMessage.hpp"
#include <string>
#include <vector>

namespace luma::client::signaling {

enum class SignalingState { Disconnected, Connecting, Connected, InRoom };

class SignalingSession {
public:
    bool BeginConnect();
    bool MarkConnected();
    bool JoinRoom(std::string room_id, std::string peer_id);
    bool LeaveRoom();
    void Disconnect();
    bool Handle(const luma::contracts::SignalingMessage& message);

    SignalingState state() const noexcept { return state_; }
    const std::string& room_id() const noexcept { return room_id_; }
    const std::string& peer_id() const noexcept { return peer_id_; }
    const std::vector<luma::contracts::SignalingMessage>& inbox() const noexcept { return inbox_; }
    void ClearInbox() { inbox_.clear(); }

private:
    SignalingState state_{SignalingState::Disconnected};
    std::string room_id_;
    std::string peer_id_;
    std::vector<luma::contracts::SignalingMessage> inbox_;
};

} // namespace luma::client::signaling
