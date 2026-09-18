#pragma once
#include "SignalingRoom.hpp"
#include "runtime-contracts/SignalingWire.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace luma::server::signaling {

class TcpSignalingServer final {
public:
    TcpSignalingServer();
    ~TcpSignalingServer();
    bool Start(std::uint16_t port);
    void Stop();
    bool IsRunning() const noexcept { return running_.load(); }
    std::size_t RoomCount() const;
    std::size_t PeerCount(const std::string& room_id) const;
private:
    struct Client { int socket{-1}; std::string room; std::string peer; };
    void AcceptLoop();
    void ClientLoop(int socket);
    void HandleMessage(Client& client, const luma::contracts::SignalingMessage& message);
    void Broadcast(const luma::contracts::SignalingMessage& message, const std::string& room, const std::string& exclude_peer = {});
    bool Send(int socket, const luma::contracts::SignalingMessage& message);
    void RemoveClient(int socket);
    int listen_socket_{-1};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    mutable std::mutex mutex_;
    std::unordered_map<int, Client> clients_;
    std::unordered_map<std::string, std::unordered_set<std::string>> rooms_;
};

} // namespace luma::server::signaling
