#pragma once
#include "SignalingMessage.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
using luma_client_socket_t = SOCKET;
constexpr luma_client_socket_t kLumaClientInvalidSocket = INVALID_SOCKET;
#else
using luma_client_socket_t = int;
constexpr luma_client_socket_t kLumaClientInvalidSocket = -1;
#endif

namespace luma::client::signaling {

class TcpSignalingClient final {
public:
    using MessageHandler = std::function<void(const luma::contracts::SignalingMessage&)>;
    TcpSignalingClient();
    ~TcpSignalingClient();
    bool Connect(const std::string& host, std::uint16_t port, MessageHandler handler);
    bool Send(const luma::contracts::SignalingMessage& message);
    void Close();
    bool IsConnected() const noexcept { return connected_.load(); }
private:
    void ReceiveLoop();
    luma_client_socket_t socket_{kLumaClientInvalidSocket};
    std::atomic<bool> connected_{false};
    std::thread receive_thread_;
    MessageHandler handler_;
    mutable std::mutex send_mutex_;
};

} // namespace luma::client::signaling
