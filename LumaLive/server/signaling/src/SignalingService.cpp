#include "../include/ISignalingService.hpp"
#include <unordered_map>

namespace luma::server::signaling {
class SignalingServiceImpl final : public ISignalingService {
public:
    shared::contracts::Result Start() override { if (running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState); running_=true; return shared::contracts::Result::Ok(); }
    shared::contracts::Result Stop() override { if (!running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState); rooms_.clear(); running_=false; return shared::contracts::Result::Ok(); }
    shared::contracts::Result CreateRoom(const std::string& room_id) override {
        if (!running_ || room_id.empty() || rooms_.contains(room_id)) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidArgument);
        rooms_.emplace(room_id, SignalingRoom{room_id}); return shared::contracts::Result::Ok();
    }
    shared::contracts::Result JoinRoom(const std::string& room_id, const std::string& peer_id) override {
        if (!running_ || room_id.empty() || peer_id.empty()) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidArgument);
        auto it=rooms_.find(room_id); if (it==rooms_.end()) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidArgument);
        if (!it->second.AddPeer(peer_id)) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState);
        return shared::contracts::Result::Ok();
    }
    shared::contracts::Result LeaveRoom(const std::string& room_id, const std::string& peer_id) override {
        auto it=rooms_.find(room_id); if (!running_ || it==rooms_.end() || !it->second.RemovePeer(peer_id)) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidArgument);
        if (it->second.PeerCount()==0) rooms_.erase(it); return shared::contracts::Result::Ok();
    }
    std::size_t PeerCount(const std::string& room_id) const override { auto it=rooms_.find(room_id); return it==rooms_.end()?0:it->second.PeerCount(); }
private: bool running_{false}; std::unordered_map<std::string, SignalingRoom> rooms_;
};
}
