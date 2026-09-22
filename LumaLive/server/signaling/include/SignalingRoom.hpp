#pragma once
#include <cstddef>
#include <string>
#include <unordered_set>

namespace luma::server::signaling {

class SignalingRoom {
public:
    explicit SignalingRoom(std::string id) : id_(std::move(id)) {}
    const std::string& id() const noexcept { return id_; }
    bool AddPeer(const std::string& peer_id) { return !peer_id.empty() && peers_.insert(peer_id).second; }
    bool RemovePeer(const std::string& peer_id) { return peers_.erase(peer_id) != 0; }
    bool HasPeer(const std::string& peer_id) const { return peers_.contains(peer_id); }
    std::size_t PeerCount() const noexcept { return peers_.size(); }
private:
    std::string id_;
    std::unordered_set<std::string> peers_;
};

} // namespace luma::server::signaling
