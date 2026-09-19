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
bool send_all(luma_socket_t s,const std::uint8_t*p,std::size_t n){while(n){int r=::send(s,reinterpret_cast<const char*>(p),static_cast<int>(n),MSG_NOSIGNAL);if(r<=0)return false;p+=r;n-=static_cast<std::size_t>(r);}return true;}
bool recv_all(luma_socket_t s,std::uint8_t*p,std::size_t n){while(n){int r=::recv(s,reinterpret_cast<char*>(p),static_cast<int>(n),0);if(r<=0)return false;p+=r;n-=static_cast<std::size_t>(r);}return true;}
}
namespace luma::server::signaling {
TcpSignalingServer::TcpSignalingServer(){sockets();}
TcpSignalingServer::~TcpSignalingServer(){Stop();}
bool TcpSignalingServer::Start(std::uint16_t port){std::lock_guard lifecycle_lock(lifecycle_mutex_);if(running_||!sockets())return false;luma_socket_t s=::socket(AF_INET,SOCK_STREAM,0);if(!valid_socket(s))return false;int yes=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&yes),sizeof(yes));sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);a.sin_port=htons(port);if(bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||listen(s,32)!=0){close_socket(s);return false;}{std::lock_guard lock(listen_mutex_);listen_socket_=s;}running_=true;accept_thread_=std::thread(&TcpSignalingServer::AcceptLoop,this);return true;}
void TcpSignalingServer::Stop(){std::lock_guard lifecycle_lock(lifecycle_mutex_);if(!running_.load()&&!accept_thread_.joinable())return;running_=false;{std::lock_guard lock(listen_mutex_);if(valid_socket(listen_socket_)){shutdown(listen_socket_,2);close_socket(listen_socket_);listen_socket_=kLumaInvalidSocket;}}if(accept_thread_.joinable())accept_thread_.join();{std::lock_guard lock(mutex_);for(const auto&[s,c]:clients_){(void)c;shutdown(s,2);}}for(auto& thread:client_threads_)if(thread.joinable())thread.join();client_threads_.clear();{std::lock_guard lock(mutex_);clients_.clear();rooms_.clear();}}
std::size_t TcpSignalingServer::RoomCount()const{std::lock_guard lock(mutex_);return rooms_.size();}
std::size_t TcpSignalingServer::PeerCount(const std::string&r)const{std::lock_guard lock(mutex_);auto it=rooms_.find(r);return it==rooms_.end()?0:it->second.size();}
void TcpSignalingServer::AcceptLoop(){while(running_){sockaddr_storage a{};
#ifdef _WIN32
int n=sizeof(a);
#else
socklen_t n=sizeof(a);
#endif
luma_socket_t listen_socket=kLumaInvalidSocket;{std::lock_guard lock(listen_mutex_);listen_socket=listen_socket_;}if(!valid_socket(listen_socket))break;luma_socket_t s=accept(listen_socket,reinterpret_cast<sockaddr*>(&a),&n);if(!valid_socket(s)){if(running_)continue;break;}std::lock_guard lock(mutex_);clients_.emplace(s,Client{s,{},{}});client_threads_.emplace_back(&TcpSignalingServer::ClientLoop,this,s);}}
bool TcpSignalingServer::Send(luma_socket_t socket,const luma::contracts::SignalingMessage&m){std::lock_guard lock(send_mutex_);auto b=luma::contracts::wire::encode(m);if(b.size()>256*1024)return false;std::uint32_t n=htonl(static_cast<std::uint32_t>(b.size()));return send_all(socket,reinterpret_cast<std::uint8_t*>(&n),4)&&send_all(socket,b.data(),b.size());}
void TcpSignalingServer::Broadcast(const luma::contracts::SignalingMessage&m,const std::string&r,const std::string&exclude){std::vector<luma_socket_t>targets;{std::lock_guard lock(mutex_);for(auto&[s,c]:clients_)if(c.room==r&&!c.peer.empty()&&c.peer!=exclude)targets.push_back(s);}for(auto s:targets)Send(s,m);}
void TcpSignalingServer::RemoveClient(luma_socket_t s){Client c;{std::lock_guard lock(mutex_);auto it=clients_.find(s);if(it==clients_.end())return;c=it->second;clients_.erase(it);if(!c.room.empty()&&!c.peer.empty()){auto rit=rooms_.find(c.room);if(rit!=rooms_.end()){rit->second.erase(c.peer);if(rit->second.empty())rooms_.erase(rit);}}}if(!c.room.empty()&&!c.peer.empty()){luma::contracts::SignalingMessage m;m.type=luma::contracts::SignalingMessageType::PeerLeft;m.room_id=c.room;m.peer_id=c.peer;Broadcast(m,c.room,c.peer);}}
void TcpSignalingServer::ClientLoop(luma_socket_t s){while(running_){std::uint32_t n=0;if(!recv_all(s,reinterpret_cast<std::uint8_t*>(&n),4))break;n=ntohl(n);if(n==0||n>256*1024)break;std::vector<std::uint8_t>b(n);if(!recv_all(s,b.data(),n))break;luma::contracts::SignalingMessage m;if(!luma::contracts::wire::decode(b,m))break;Client c;{std::lock_guard lock(mutex_);auto it=clients_.find(s);if(it==clients_.end())break;c=it->second;}HandleMessage(c,m);{std::lock_guard lock(mutex_);if(clients_.find(s)==clients_.end())break;}}close_socket(s);RemoveClient(s);}
void TcpSignalingServer::HandleMessage(Client&c,const luma::contracts::SignalingMessage&m){using T=luma::contracts::SignalingMessageType;if(m.type==T::Ping){luma::contracts::SignalingMessage p;p.type=T::Pong;Send(c.socket,p);return;}if(m.type==T::JoinRoom){if(m.room_id.empty()||m.peer_id.empty())return;bool added=false;std::vector<std::string> existing;{std::lock_guard lock(mutex_);auto it=clients_.find(c.socket);if(it==clients_.end())return;if(!it->second.room.empty())return;auto&peers=rooms_[m.room_id];existing.assign(peers.begin(),peers.end());added=peers.insert(m.peer_id).second;if(!added)return;it->second.room=m.room_id;it->second.peer=m.peer_id;c=it->second;}for(const auto& peer:existing){luma::contracts::SignalingMessage notice;notice.type=T::PeerJoined;notice.room_id=c.room;notice.peer_id=peer;notice.target_peer_id=c.peer;Send(c.socket,notice);}luma::contracts::SignalingMessage joined;joined.type=T::PeerJoined;joined.room_id=c.room;joined.peer_id=c.peer;Broadcast(joined,c.room,c.peer);return;}if(c.room.empty()||c.peer.empty()||(!m.room_id.empty()&&m.room_id!=c.room)||(!m.peer_id.empty()&&m.peer_id!=c.peer))return;if(m.type==T::LeaveRoom){RemoveClient(c.socket);return;}if(m.type==T::Offer||m.type==T::Answer||m.type==T::IceCandidate){if(m.target_peer_id.empty())return;luma::contracts::SignalingMessage f=m;f.peer_id=c.peer;f.room_id=c.room;std::vector<luma_socket_t>targets;{std::lock_guard lock(mutex_);for(auto&[sock,other]:clients_)if(other.room==c.room&&other.peer==m.target_peer_id)targets.push_back(sock);}for(auto sock:targets)Send(sock,f);return;}}
}