#include "TcpSignalingClient.hpp"
#include "runtime-contracts/SignalingWireCodec.hpp"
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_len_t = int;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_len_t = socklen_t;
#endif

namespace {
#ifdef _WIN32
bool init_sockets() {
    static bool ok = [] {
        WSADATA d{};
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
}
void close_socket(luma_client_socket_t s) { closesocket(s); }
#else
bool init_sockets() { return true; }
void close_socket(luma_client_socket_t s) { ::close(s); }
#endif

bool valid_socket(luma_client_socket_t s) {
#ifdef _WIN32
    return s != INVALID_SOCKET;
#else
    return s >= 0;
#endif
}

bool send_all(luma_client_socket_t s, const std::uint8_t* p, std::size_t n) {
    while (n) {
        const int r = ::send(s, reinterpret_cast<const char*>(p), static_cast<int>(n), 0);
        if (r <= 0) return false;
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

bool recv_all(luma_client_socket_t s, std::uint8_t* p, std::size_t n) {
    while (n) {
        const int r = ::recv(s, reinterpret_cast<char*>(p), static_cast<int>(n), 0);
        if (r <= 0) return false;
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}
} // namespace

namespace luma::client::signaling {

TcpSignalingClient::TcpSignalingClient() { init_sockets(); }

TcpSignalingClient::~TcpSignalingClient() { Close(); }

void TcpSignalingClient::JoinReceiveThreadIfNeeded() {
    if (receive_thread_.joinable() &&
        receive_thread_.get_id() != std::this_thread::get_id()) {
        receive_thread_.join();
    }
}

void TcpSignalingClient::CloseSocketLocked() {
    if (!valid_socket(socket_)) return;
    close_socket(socket_);
    socket_ = kLumaClientInvalidSocket;
}

bool TcpSignalingClient::Connect(
    const std::string& host,
    std::uint16_t port,
    MessageHandler handler) {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);

    if (connected_.load()) return false;
    JoinReceiveThreadIfNeeded();

    {
        std::lock_guard socket_lock(socket_mutex_);
        CloseSocketLocked();
    }

    if (!init_sockets()) return false;
    handler_ = std::move(handler);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string ps = std::to_string(port);
    if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0) return false;

    luma_client_socket_t connected_socket = kLumaClientInvalidSocket;
    for (auto* p = res; p; p = p->ai_next) {
        luma_client_socket_t s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (!valid_socket(s)) continue;
        if (::connect(s, p->ai_addr, static_cast<socket_len_t>(p->ai_addrlen)) == 0) {
            connected_socket = s;
            break;
        }
        close_socket(s);
    }
    freeaddrinfo(res);

    if (!valid_socket(connected_socket)) return false;

    {
        std::lock_guard socket_lock(socket_mutex_);
        socket_ = connected_socket;
    }

    connected_.store(true);
    receive_thread_ = std::thread(&TcpSignalingClient::ReceiveLoop, this);
    return true;
}

bool TcpSignalingClient::Send(const luma::contracts::SignalingMessage& message) {
    std::lock_guard send_lock(send_mutex_);
    if (!connected_.load()) return false;

    auto payload = luma::contracts::wire::encode(message);
    if (payload.size() > 256 * 1024) return false;

    std::uint32_t n = htonl(static_cast<std::uint32_t>(payload.size()));
    luma_client_socket_t socket = kLumaClientInvalidSocket;
    {
        std::lock_guard socket_lock(socket_mutex_);
        socket = socket_;
    }
    if (!valid_socket(socket)) return false;

    return send_all(socket, reinterpret_cast<std::uint8_t*>(&n), 4) &&
           send_all(socket, payload.data(), payload.size());
}

void TcpSignalingClient::ReceiveLoop() {
    luma_client_socket_t socket = kLumaClientInvalidSocket;
    {
        std::lock_guard socket_lock(socket_mutex_);
        socket = socket_;
    }
    if (!valid_socket(socket)) {
        connected_.store(false);
        return;
    }

    while (connected_.load()) {
        std::uint32_t n = 0;
        if (!recv_all(socket, reinterpret_cast<std::uint8_t*>(&n), 4)) break;
        n = ntohl(n);
        if (n == 0 || n > 256 * 1024) break;

        std::vector<std::uint8_t> buffer(n);
        if (!recv_all(socket, buffer.data(), buffer.size())) break;

        luma::contracts::SignalingMessage message;
        if (luma::contracts::wire::decode(buffer, message) && handler_) {
            handler_(message);
        }
    }

    connected_.store(false);
}

void TcpSignalingClient::Close() {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    connected_.store(false);

    luma_client_socket_t socket = kLumaClientInvalidSocket;
    {
        std::lock_guard socket_lock(socket_mutex_);
        socket = socket_;
    }
    if (valid_socket(socket)) {
#ifdef _WIN32
        shutdown(socket, SD_BOTH);
#else
        shutdown(socket, SHUT_RDWR);
#endif
    }

    const bool called_from_receive_thread =
        receive_thread_.joinable() &&
        receive_thread_.get_id() == std::this_thread::get_id();
    if (called_from_receive_thread) {
        return;
    }

    JoinReceiveThreadIfNeeded();

    std::lock_guard socket_lock(socket_mutex_);
    CloseSocketLocked();
}

} // namespace luma::client::signaling
