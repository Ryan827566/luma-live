#include "TcpSignalingClient.hpp"
#include "runtime-contracts/SignalingWireCodec.hpp"
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
bool init_sockets() { static bool ok = []{ WSADATA d{}; return WSAStartup(MAKEWORD(2,2), &d) == 0; }(); return ok; }
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
    while (n) { int r = ::send(s, reinterpret_cast<const char*>(p), static_cast<int>(n), 0); if (r <= 0) return false; p += r; n -= static_cast<std::size_t>(r); } return true;
}
bool recv_all(luma_client_socket_t s, std::uint8_t* p, std::size_t n) {
    while (n) { int r = ::recv(s, reinterpret_cast<char*>(p), static_cast<int>(n), 0); if (r <= 0) return false; p += r; n -= static_cast<std::size_t>(r); } return true;
}
}

namespace luma::client::signaling {
TcpSignalingClient::TcpSignalingClient() { init_sockets(); }
TcpSignalingClient::~TcpSignalingClient(){ Close(); }
bool TcpSignalingClient::Connect(const std::string& host,std::uint16_t port,MessageHandler handler){
    if(connected_) return false; if(!init_sockets()) return false; handler_=std::move(handler);
    addrinfo hints{}; hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM; addrinfo* res=nullptr; std::string ps=std::to_string(port);
    if(getaddrinfo(host.c_str(),ps.c_str(),&hints,&res)!=0) return false;
    for(auto* p=res;p;p=p->ai_next){ luma_client_socket_t s=::socket(p->ai_family,p->ai_socktype,p->ai_protocol); if(!valid_socket(s)) continue; if(::connect(s,p->ai_addr,static_cast<socket_len_t>(p->ai_addrlen))==0){socket_=s;break;} close_socket(s); }
    freeaddrinfo(res); if(!valid_socket(socket_))return false; connected_=true; receive_thread_=std::thread(&TcpSignalingClient::ReceiveLoop,this); return true;
}
bool TcpSignalingClient::Send(const luma::contracts::SignalingMessage&m){
    std::lock_guard lock(send_mutex_);
    if(!connected_)return false;
    auto payload=luma::contracts::wire::encode(m);
    if(payload.size()>256*1024)return false;
    std::uint32_t n=htonl(static_cast<std::uint32_t>(payload.size()));
    return send_all(socket_,reinterpret_cast<std::uint8_t*>(&n),4)&&send_all(socket_,payload.data(),payload.size());
}
void TcpSignalingClient::ReceiveLoop(){
    while(connected_){
        std::uint32_t n=0;
        if(!recv_all(socket_,reinterpret_cast<std::uint8_t*>(&n),4))break;
        n=ntohl(n);
        if(n==0||n>256*1024)break;
        std::vector<std::uint8_t>b(n);
        if(!recv_all(socket_,b.data(),b.size()))break;
        luma::contracts::SignalingMessage m;
        if(luma::contracts::wire::decode(b,m)&&handler_)handler_(m);
    }
    connected_=false;
}
void TcpSignalingClient::Close(){if(!connected_&& !valid_socket(socket_)){if(receive_thread_.joinable())receive_thread_.join();return;}connected_=false;if(valid_socket(socket_)){shutdown(socket_,2);close_socket(socket_);socket_=kLumaClientInvalidSocket;}if(receive_thread_.joinable())receive_thread_.join();}
}
