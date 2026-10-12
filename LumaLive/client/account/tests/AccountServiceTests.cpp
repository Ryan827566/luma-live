#include "IAccountService.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
using Socket=SOCKET;
constexpr Socket kInvalidSocket=INVALID_SOCKET;
void CloseSocket(Socket socket){::closesocket(socket);}
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket=int;
constexpr Socket kInvalidSocket=-1;
void CloseSocket(Socket socket){::close(socket);}
#endif

int main() {
    using namespace luma::client::account;
    OperationResult result{false, ""};
    assert(!result.success);

#ifdef _WIN32
    WSADATA sockets{};
    assert(::WSAStartup(MAKEWORD(2,2),&sockets)==0);
#endif
    const Socket listener=::socket(AF_INET,SOCK_STREAM,0);
    assert(listener!=kInvalidSocket);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    assert(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    assert(::listen(listener,1)==0);
#ifdef _WIN32
    int address_length=sizeof(address);
#else
    socklen_t address_length=sizeof(address);
#endif
    assert(::getsockname(listener,reinterpret_cast<sockaddr*>(&address),&address_length)==0);

    std::mutex mutex;
    std::condition_variable completed;
    bool release_peer=false;
    std::promise<void> accepted;
    auto accepted_future=accepted.get_future();
    std::thread stalled_peer([&]{
        const Socket peer=::accept(listener,nullptr,nullptr);
        assert(peer!=kInvalidSocket);
        accepted.set_value();
        // This peer accepts the connection but never replies to the login request.
        // The fallback deadline also lets a regressed client fail without hanging the suite.
        std::unique_lock lock(mutex);
        completed.wait_for(lock,std::chrono::seconds(8),[&]{return release_peer;});
        CloseSocket(peer);
    });

    auto service=CreateAccountService();
    assert(service->Connect("127.0.0.1",ntohs(address.sin_port)).success);
    accepted_future.wait();
    const auto started=std::chrono::steady_clock::now();
    const auto login=service->Login("timeout.user","correct horse");
    const auto elapsed=std::chrono::steady_clock::now()-started;
    {
        std::lock_guard lock(mutex);
        release_peer=true;
    }
    completed.notify_one();
    stalled_peer.join();
    CloseSocket(listener);

    assert(!login.success);
    assert(elapsed<std::chrono::seconds(7));
    assert(!service->IsRunning());
    assert(!service->IsAuthenticated());
    assert(service->Session().token.empty());
    const auto stop_started=std::chrono::steady_clock::now();
    assert(service->Stop().success);
    assert(std::chrono::steady_clock::now()-stop_started<std::chrono::seconds(1));
#ifdef _WIN32
    ::WSACleanup();
#endif
    return 0;
}
