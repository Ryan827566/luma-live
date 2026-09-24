#include <vector>
#include "TcpSignalingServer.hpp"
#include "runtime-contracts/SignalingWireCodec.hpp"
#include <algorithm>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#pragma comment(lib,"Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace {
#ifdef _WIN32
void close_socket(luma_socket_t s){closesocket(s);}
bool sockets(){static bool ok=[](){WSADATA d{};return WSAStartup(MAKEWORD(2,2),&d)==0;}();return ok;}
#else
void close_socket(luma_socket_t s){::close(s);} bool sockets(){return true;}
#endif
bool valid_socket(luma_socket_t s){
#ifdef _WIN32
return s != INVALID_SOCKET;
#else
return s >= 0;
#endif
}
bool set_blocking(luma_socket_t s,bool blocking) {
#ifdef _WIN32
    u_long mode=blocking?0:1;return ioctlsocket(s,FIONBIO,&mode)==0;
#else
    const int flags=fcntl(s,F_GETFL,0);return flags>=0&&fcntl(s,F_SETFL,blocking?(flags&~O_NONBLOCK):(flags|O_NONBLOCK))==0;
#endif
}
bool send_all(luma_socket_t s,const std::uint8_t*p,std::size_t n){while(n){int r=::send(s,reinterpret_cast<const char*>(p),static_cast<int>(n),MSG_NOSIGNAL);if(r<=0)return false;p+=r;n-=static_cast<std::size_t>(r);}return true;}
// Recheck cancellation between receive attempts, including partial wire frames.
bool recv_all(luma_socket_t s,std::uint8_t* p,std::size_t n,const std::atomic<bool>& running) {
    while(n && running.load()) {
        fd_set readable; FD_ZERO(&readable); FD_SET(s,&readable);
        timeval wait{0,100000};
#ifdef _WIN32
        const int ready=select(0,&readable,nullptr,nullptr,&wait);
#else
        const int ready=select(s+1,&readable,nullptr,nullptr,&wait);
#endif
        if(!running.load() || ready<0)return false;
        if(ready==0)continue;
        const int received=recv(s,reinterpret_cast<char*>(p),static_cast<int>(n),0);
        if(received<=0)return false;
        p+=received;n-=static_cast<std::size_t>(received);
    }
    return n==0;
}
}
namespace luma::server::signaling {
TcpSignalingServer::TcpSignalingServer(){sockets();}
TcpSignalingServer::~TcpSignalingServer(){Stop();}
bool TcpSignalingServer::Start(std::uint16_t port){std::lock_guard lifecycle_lock(lifecycle_mutex_);if(running_||!sockets())return false;luma_socket_t s=::socket(AF_INET,SOCK_STREAM,0);if(!valid_socket(s))return false;int yes=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&yes),sizeof(yes));sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);a.sin_port=htons(port);if(bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||listen(s,32)!=0||!set_blocking(s,false)){close_socket(s);return false;}{std::lock_guard lock(listen_mutex_);listen_socket_=s;}running_=true;accept_thread_=std::thread(&TcpSignalingServer::AcceptLoop,this);return true;}
void TcpSignalingServer::Stop() {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if(!running_.load()&&!accept_thread_.joinable())return;
    running_=false;
    if(accept_thread_.joinable())accept_thread_.join();
    {std::lock_guard lock(listen_mutex_);
        if(valid_socket(listen_socket_)){close_socket(listen_socket_);listen_socket_=kLumaInvalidSocket;}}
    {std::lock_guard lock(mutex_);for(const auto& [socket,client]:clients_){(void)client;shutdown(socket,2);}}
    for(auto& thread:client_threads_)if(thread.joinable())thread.join();
    client_threads_.clear();
    {std::lock_guard lock(mutex_);clients_.clear();rooms_.clear();}
}
std::size_t TcpSignalingServer::RoomCount()const{std::lock_guard lock(mutex_);return rooms_.size();}
std::size_t TcpSignalingServer::PeerCount(const std::string&r)const{std::lock_guard lock(mutex_);auto it=rooms_.find(r);return it==rooms_.end()?0:it->second.size();}
void TcpSignalingServer::AcceptLoop(){while(running_){sockaddr_storage a{};
#ifdef _WIN32
int n=sizeof(a);
#else
socklen_t n=sizeof(a);
#endif
luma_socket_t listen_socket=kLumaInvalidSocket;{std::lock_guard lock(listen_mutex_);listen_socket=listen_socket_;}if(!valid_socket(listen_socket))break;
fd_set readable;FD_ZERO(&readable);FD_SET(listen_socket,&readable);timeval wait{0,100000};
#ifdef _WIN32
const int ready=select(0,&readable,nullptr,nullptr,&wait);
#else
const int ready=select(listen_socket+1,&readable,nullptr,nullptr,&wait);
#endif
if(!running_)break;if(ready<=0)continue;
luma_socket_t s=accept(listen_socket,reinterpret_cast<sockaddr*>(&a),&n);if(!valid_socket(s)){if(running_)continue;break;}std::lock_guard lock(mutex_);
#ifdef _WIN32
const DWORD send_timeout=1500;
#else
const timeval send_timeout{1,500000};
#endif
if(!set_blocking(s,true)||setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&send_timeout),sizeof(send_timeout))!=0){close_socket(s);continue;}
clients_.emplace(s,Client{s,{},{}});client_threads_.emplace_back(&TcpSignalingServer::ClientLoop,this,s);}}
// Each connection owns its write lock and lifetime token. A slow recipient must
// not hold up unrelated rooms, and a recycled socket must not receive old SDP.
bool TcpSignalingServer::Send(const Client& client,const luma::contracts::SignalingMessage&m){std::lock_guard lock(client.send->mutex);if(!client.send->open)return false;const auto socket=client.socket;auto b=luma::contracts::wire::encode(m);if(b.size()>256*1024)return false;std::uint32_t n=htonl(static_cast<std::uint32_t>(b.size()));const bool sent=send_all(socket,reinterpret_cast<std::uint8_t*>(&n),4)&&send_all(socket,b.data(),b.size());if(!sent)shutdown(socket,2);return sent;}
void TcpSignalingServer::Broadcast(const luma::contracts::SignalingMessage&m,const std::string&r,const std::string&exclude){std::vector<Client>targets;{std::lock_guard lock(mutex_);for(auto&[s,c]:clients_)if(c.room==r&&!c.peer.empty()&&c.peer!=exclude)targets.push_back(c);}for(auto s:targets)Send(s,m);}
void TcpSignalingServer::RemoveClient(luma_socket_t s){Client c;{std::lock_guard lock(mutex_);auto it=clients_.find(s);if(it==clients_.end())return;c=it->second;clients_.erase(it);if(!c.room.empty()&&!c.peer.empty()){auto rit=rooms_.find(c.room);if(rit!=rooms_.end()){rit->second.erase(c.peer);if(rit->second.empty())rooms_.erase(rit);}}}if(!c.room.empty()&&!c.peer.empty()){luma::contracts::SignalingMessage m;m.type=luma::contracts::SignalingMessageType::PeerLeft;m.room_id=c.room;m.peer_id=c.peer;Broadcast(m,c.room,c.peer);}}
void TcpSignalingServer::ClientLoop(luma_socket_t s){Client connection;{std::lock_guard lock(mutex_);auto it=clients_.find(s);if(it==clients_.end())return;connection=it->second;}while(running_){std::uint32_t n=0;if(!recv_all(s,reinterpret_cast<std::uint8_t*>(&n),4,running_))break;n=ntohl(n);if(n==0||n>256*1024)break;std::vector<std::uint8_t>b(n);if(!recv_all(s,b.data(),n,running_))break;luma::contracts::SignalingMessage m;if(!luma::contracts::wire::decode(b,m))break;Client c;{std::lock_guard lock(mutex_);auto it=clients_.find(s);if(it==clients_.end())break;c=it->second;}HandleMessage(c,m);{std::lock_guard lock(mutex_);if(clients_.find(s)==clients_.end())break;}}RemoveClient(s);{std::lock_guard lock(connection.send->mutex);connection.send->open=false;close_socket(s);}}
void TcpSignalingServer::HandleMessage(Client& c, const luma::contracts::SignalingMessage& m) {
    using T = luma::contracts::SignalingMessageType;
    auto error = [&](const std::string& reason) {
        luma::contracts::SignalingMessage response;
        response.type = T::Error;
        response.room_id = c.room.empty() ? m.room_id : c.room;
        response.target_peer_id = c.peer.empty() ? m.peer_id : c.peer;
        response.sequence = m.sequence; response.value = reason;
        Send(c, response);
    };
    if (m.type == T::Ping) {
        luma::contracts::SignalingMessage pong; pong.type = T::Pong;
        Send(c, pong); return;
    }
    if (m.type == T::JoinRoom) {
        if (m.room_id.empty() || m.peer_id.empty() || m.room_id.size() > 128 || m.peer_id.size() > 128) {
            error("Room and participant IDs must contain 1 to 128 bytes"); return;
        }
        bool added = false;
        std::vector<std::string> existing;
        {
            std::lock_guard lock(mutex_);
            auto it = clients_.find(c.socket);
            if (it == clients_.end()) return;
            if (it->second.room.empty()) {
                auto& peers = rooms_[m.room_id];
                existing.assign(peers.begin(), peers.end());
                added = peers.insert(m.peer_id).second;
                if (added) {
                    it->second.room = m.room_id; it->second.peer = m.peer_id;
                    c = it->second;
                }
            }
        }
        if (!added) { error("Participant ID is already in use; choose another ID"); return; }
        // Acknowledgment distinguishes registered identity from an open TCP socket.
        luma::contracts::SignalingMessage ack;
        ack.type = T::RoomJoined; ack.room_id = c.room; ack.target_peer_id = c.peer;
        Send(c, ack);
        for (const auto& peer : existing) {
            luma::contracts::SignalingMessage notice;
            notice.type = T::PeerJoined; notice.room_id = c.room;
            notice.peer_id = peer; notice.target_peer_id = c.peer;
            Send(c, notice);
        }
        luma::contracts::SignalingMessage joined;
        joined.type = T::PeerJoined; joined.room_id = c.room; joined.peer_id = c.peer;
        Broadcast(joined, c.room, c.peer); return;
    }
    // Never trust sender identity supplied in a forwarded packet.
    if (c.room.empty() || c.peer.empty() || m.room_id != c.room || m.peer_id != c.peer) {
        error("Sender identity does not match the registered connection"); return;
    }
    if (m.type == T::LeaveRoom) { RemoveClient(c.socket); return; }
    const bool routed = m.type == T::Offer || m.type == T::Answer || m.type == T::IceCandidate ||
        m.type == T::CallInvite || m.type == T::CallAccept || m.type == T::CallReject ||
        m.type == T::CallCancel || m.type == T::CallHangup || m.type == T::CallBusy ||
        m.type == T::CallMediaState || m.type == T::CallReconnect;
    if (!routed) return;
    if (m.target_peer_id.empty() || m.target_peer_id == c.peer) {
        error("A different target participant is required"); return;
    }
    const bool callControl = m.type != T::Offer && m.type != T::Answer && m.type != T::IceCandidate;
    if (callControl && m.sequence <= 0) { error("A positive call ID is required"); return; }
    luma::contracts::SignalingMessage forwarded = m;
    forwarded.peer_id = c.peer; forwarded.room_id = c.room;
    std::vector<Client> targets;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [socket, other] : clients_)
            if (other.room == c.room && other.peer == m.target_peer_id) targets.push_back(other);
    }
    if (targets.empty()) { error("Target participant is no longer in the room"); return; }
    for (auto socket : targets) Send(socket, forwarded);
}

}

