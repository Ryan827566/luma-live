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
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
using luma_socket_t = SOCKET;
constexpr luma_socket_t kLumaInvalidSocket = INVALID_SOCKET;
#else
using luma_socket_t = int;
constexpr luma_socket_t kLumaInvalidSocket = -1;
#endif

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
    struct Client { luma_socket_t socket{kLumaInvalidSocket}; std::string room; std::string peer; };
    void AcceptLoop();
    void ClientLoop(luma_socket_t socket);
    void HandleMessage(Client& client, const luma::contracts::SignalingMessage& message);
    void Broadcast(const luma::contracts::SignalingMessage& message, const std::string& room, const std::string& exclude_peer = {});
    bool Send(luma_socket_t socket, const luma::contracts::SignalingMessage& message);
    void RemoveClient(luma_socket_t socket);
    luma_socket_t listen_socket_{kLumaInvalidSocket};
    std::atomic<bool> running_{false};
    mutable std::mutex lifecycle_mutex_;
    std::thread accept_thread_;
    mutable std::mutex mutex_;
    mutable std::mutex send_mutex_;
    mutable std::mutex listen_mutex_;
    std::unordered_map<luma_socket_t, Client> clients_;
    std::unordered_map<std::string, std::unordered_set<std::string>> rooms_;
    std::vector<std::thread> client_threads_;
};

} // namespace luma::server::signaling
