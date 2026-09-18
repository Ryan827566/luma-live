#pragma once
#include "contracts/errors/Error.hpp"
#include "SignalingRoom.hpp"
#include <memory>
#include <string>
#include <vector>

namespace luma::server::signaling {
class ISignalingService {
public:
    virtual ~ISignalingService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
    virtual shared::contracts::Result CreateRoom(const std::string& room_id) = 0;
    virtual shared::contracts::Result JoinRoom(const std::string& room_id, const std::string& peer_id) = 0;
    virtual shared::contracts::Result LeaveRoom(const std::string& room_id, const std::string& peer_id) = 0;
    virtual std::size_t PeerCount(const std::string& room_id) const = 0;
};
}
