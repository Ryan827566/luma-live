#pragma once
#include "SignalingMessage.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

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
    int socket_{-1};
    std::atomic<bool> connected_{false};
    std::thread receive_thread_;
    MessageHandler handler_;
};

} // namespace luma::client::signaling
