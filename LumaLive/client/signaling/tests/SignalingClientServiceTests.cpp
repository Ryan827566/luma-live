#include "TcpSignalingClient.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
using test_socket_t = SOCKET;
using socket_len_t = int;
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using test_socket_t = int;
using socket_len_t = socklen_t;
#endif

namespace {
void close_test_socket(test_socket_t socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

bool valid_test_socket(test_socket_t socket) {
#ifdef _WIN32
    return socket != INVALID_SOCKET;
#else
    return socket >= 0;
#endif
}
} // namespace

int main() {
    using namespace luma::client::signaling;
    using namespace std::chrono_literals;

    TcpSignalingClient client;
    assert(!client.IsConnected());

    // Regression test: a receive thread that exits asynchronously must be
    // joined before the next Connect(), otherwise std::thread assignment
    // would call std::terminate.
    test_socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(valid_test_socket(listener));

    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
                 &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    assert(::listen(listener, 2) == 0);

    socket_len_t address_length = sizeof(address);
    assert(::getsockname(
               listener,
               reinterpret_cast<sockaddr*>(&address),
               &address_length) == 0);
    const auto port = ntohs(address.sin_port);

    std::mutex ready_mutex;
    std::condition_variable ready_cv;
    bool server_ready = false;
    std::atomic<int> accepted{0};

    std::thread server([&] {
        {
            std::lock_guard lock(ready_mutex);
            server_ready = true;
        }
        ready_cv.notify_one();

        for (int i = 0; i < 2; ++i) {
            test_socket_t peer = ::accept(listener, nullptr, nullptr);
            if (!valid_test_socket(peer)) break;
            ++accepted;
            std::this_thread::sleep_for(50ms);
#ifdef _WIN32
            shutdown(peer, SD_BOTH);
#else
            shutdown(peer, SHUT_RDWR);
#endif
            close_test_socket(peer);
        }
        close_test_socket(listener);
    });

    {
        std::unique_lock lock(ready_mutex);
        ready_cv.wait(lock, [&] { return server_ready; });
    }

    assert(client.Connect("127.0.0.1", port, {}));

    for (int i = 0; i < 40 && client.IsConnected(); ++i) {
        std::this_thread::sleep_for(25ms);
    }
    assert(!client.IsConnected());

    // Critical regression path: Connect() must first join the completed
    // receive thread before assigning a new std::thread.
    assert(client.Connect("127.0.0.1", port, {}));
    client.Close();

    server.join();
    assert(accepted.load() == 2);
    return 0;
}
