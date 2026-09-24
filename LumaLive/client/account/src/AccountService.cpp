#include "IAccountService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include "contracts/auth/AuthWire.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
using Socket=SOCKET; constexpr Socket kInvalidSocket=INVALID_SOCKET;
inline void close_socket(Socket s){::closesocket(s);}
inline bool init_sockets(){static bool once=[](){WSADATA d{};return WSAStartup(MAKEWORD(2,2),&d)==0;}();return once;}
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket=int; constexpr Socket kInvalidSocket=-1;
inline void close_socket(Socket s){::close(s);}
inline bool init_sockets(){return true;}
#endif

namespace luma::client::account {
using luma::contracts::auth::crypto::from_hex; using luma::contracts::auth::crypto::hex;
using luma::contracts::auth::crypto::hmac_sha256; using luma::contracts::auth::crypto::pbkdf2_hmac_sha256;
using luma::contracts::auth::wire::Packet; using luma::contracts::auth::wire::Type;

class AccountService final:public IAccountService{
public:
 ~AccountService()override{Stop();}
 OperationResult Start()override{return Connect(host_,port_);}
 OperationResult Connect(std::string host,std::uint16_t port)override{
  Stop();if(host.empty()||!port||!init_sockets())return{false,"invalid account server address"};host_=std::move(host);port_=port;
  Socket s=::socket(AF_INET,SOCK_STREAM,0);if(s==kInvalidSocket)return{false,"socket creation failed"};
  sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port_);
#ifdef _WIN32
  if(::InetPtonA(AF_INET,host_.c_str(),&a.sin_addr)!=1){close_socket(s);return{false,"host must be an IPv4 address"};}
#else
  if(::inet_pton(AF_INET,host_.c_str(),&a.sin_addr)!=1){close_socket(s);return{false,"host must be an IPv4 address"};}
#endif
  if(::connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0){close_socket(s);return{false,"unable to connect to auth server"};}
  socket_=s;running_=true;return{true,"connected"};
 }
 OperationResult Stop()override{std::lock_guard lock(mutex_);if(socket_!=kInvalidSocket){close_socket(socket_);socket_=kInvalidSocket;}running_=false;session_={};return{true,"stopped"};}
 bool IsRunning()const override{return running_.load();}
 OperationResult Register(std::string u,std::string e,std::string d,std::string p)override{
  if(!valid_register(u,e,d,p)) return {false,"invalid registration data"};
  std::lock_guard lock(mutex_);
  if(!running_) return {false,"account service is not connected"};
  if(!Send({Type::RegisterBegin,{u,e,d}})) return {false,"send failed"};
  Packet q;
  if(!Recv(q) || q.type==Type::Error) return Error(q);
  if(q.type!=Type::RegisterChallenge || q.fields.size()!=2) return {false,"invalid registration challenge"};
  auto verifier=pbkdf2_hmac_sha256(p,from_hex(q.fields[0]));if(!Send({Type::RegisterFinish,{hex(verifier)}}))return{false,"send failed"};if(!Recv(q)||q.type==Type::Error)return Error(q);if(q.type!=Type::RegisterOk||q.fields.size()!=4)return{false,"invalid registration response"};return{true,"registered user_id="+q.fields[0]};
 }
 OperationResult Login(std::string id,std::string p)override{
  if(id.empty() || id.size()>254 || p.size()<8 || p.size()>128) return {false,"invalid login data"};
  std::lock_guard lock(mutex_);
  if(!running_) return {false,"account service is not connected"};
  if(!Send({Type::LoginBegin,{id}})) return {false,"send failed"};
  Packet q;
  if(!Recv(q) || q.type==Type::Error) return Error(q);
  if(q.type!=Type::LoginChallenge || q.fields.size()!=5) return {false,"invalid login challenge"};
  auto verifier=pbkdf2_hmac_sha256(p,from_hex(q.fields[3]));auto proof=hmac_sha256(verifier,from_hex(q.fields[4]));if(!Send({Type::LoginProof,{hex(proof)}}))return{false,"send failed"};if(!Recv(q)||q.type==Type::Error)return Error(q);if(q.type!=Type::LoginOk||q.fields.size()!=6)return{false,"invalid login response"};
  session_.token=q.fields[0];session_.user.user_id=q.fields[1];session_.user.username=q.fields[2];session_.user.email=q.fields[3];session_.user.display_name=q.fields[4];session_.expires_at_epoch_seconds=std::stoll(q.fields[5]);return{true,"login successful"};
 }
 OperationResult Logout()override{
  std::lock_guard lock(mutex_);if(!running_)return{false,"account service is not connected"};if(session_.token.empty())return{true,"already logged out"};if(!Send({Type::Logout,{session_.token}}))return{false,"send failed"};Packet q;if(!Recv(q))return{false,"receive failed"};if(q.type==Type::Error)return Error(q);if(q.type!=Type::LogoutOk)return{false,"invalid logout response"};session_={};return{true,"logout successful"};
 }
 OperationResult ValidateSession()override{
  std::lock_guard lock(mutex_);if(!running_||session_.token.empty())return{false,"not authenticated"};if(!Send({Type::ValidateSession,{session_.token}}))return{false,"send failed"};Packet q;if(!Recv(q))return{false,"receive failed"};if(q.type==Type::Error)return Error(q);if(q.type!=Type::LoginOk||q.fields.size()!=6)return{false,"invalid session response"};
  session_.user.user_id=q.fields[1];session_.user.username=q.fields[2];session_.user.email=q.fields[3];session_.user.display_name=q.fields[4];session_.expires_at_epoch_seconds=std::stoll(q.fields[5]);return{true,"session valid"};
 }
 bool IsAuthenticated()const override{std::lock_guard lock(mutex_);return !session_.token.empty()&&session_.expires_at_epoch_seconds>std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
 contracts::auth::AuthSession Session()const override{std::lock_guard lock(mutex_);return session_;}
 OperationResult Execute(std::string_view operation)override{if(operation.empty())return{false,"operation is empty"};return{true,std::string(operation)};}
private:
 static bool valid_register(const std::string&u,const std::string&e,const std::string&d,const std::string&p){if(u.size()<3||u.size()>32||e.size()<3||e.size()>254||d.empty()||d.size()>64||p.size()<8||p.size()>128)return false;for(char c:u)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;return e.find('@')!=std::string::npos&&e.find('|')==std::string::npos&&d.find('|')==std::string::npos;}
 bool Send(const Packet&p){
  auto msg=luma::contracts::auth::wire::encode(p);
  std::cerr << "account tx size=" << msg.size() << " first=" << (msg.empty()?0:int(static_cast<unsigned char>(msg[0]))) << "\n";
  std::size_t off=0;while(off<msg.size()){int n=::send(socket_,msg.data()+off,static_cast<int>(msg.size()-off),0);if(n<=0)return false;off+=static_cast<std::size_t>(n);}return true;}
 bool Recv(Packet&out){std::string line;char c=0;while(true){int n=::recv(socket_,&c,1,0);if(n<=0)return false;if(c=='\n')break;if(line.size()>64*1024)return false;}try{out=luma::contracts::auth::wire::decode_line(line);return true;}catch(...){return false;}}
 OperationResult Error(const Packet&p){return p.fields.size()>=2?OperationResult{false,p.fields[1]}:OperationResult{false,"authentication server error"};}
 std::string host_{"127.0.0.1"};std::uint16_t port_{9100};std::atomic<bool>running_{false};Socket socket_{kInvalidSocket};mutable std::mutex mutex_;contracts::auth::AuthSession session_;
};
std::unique_ptr<IAccountService>CreateAccountService(){return std::make_unique<AccountService>();}
}
