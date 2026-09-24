#include "IAuthService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include "contracts/auth/AuthWire.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")
using Socket=SOCKET; constexpr Socket kInvalidSocket=INVALID_SOCKET;
inline void close_socket(Socket s){::closesocket(s);}
inline bool init_sockets(){static bool once=[](){WSADATA d{};return WSAStartup(MAKEWORD(2,2),&d)==0;}();return once;}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket=int; constexpr Socket kInvalidSocket=-1;
inline void close_socket(Socket s){::close(s);}
inline bool init_sockets(){return true;}
#endif

namespace luma::server::auth {
using shared::contracts::ErrorCode; using shared::contracts::Result;
using luma::contracts::auth::crypto::constant_time_equal;
using luma::contracts::auth::crypto::from_hex;
using luma::contracts::auth::crypto::hex;
using luma::contracts::auth::crypto::hmac_sha256;
using luma::contracts::auth::crypto::pbkdf2_hmac_sha256;
using luma::contracts::auth::crypto::random_bytes;
using luma::contracts::auth::wire::Packet;
using luma::contracts::auth::wire::Type;

namespace {
struct UserRecord{
    std::string id,username,email,display_name,avatar_url,salt_hex,verifier_hex;
};
struct Pending{
    enum class Kind{None,Register,Login}kind{Kind::None};
    std::string username,email,display_name,user_id,salt_hex,nonce_hex;
};
struct ClientState{Socket socket{kInvalidSocket};Pending pending;std::string token;};
struct SessionRecord{std::string user_id;std::int64_t expires{0};};

std::int64_t now_epoch(){
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool valid_username(std::string_view s){
    if(s.size()<3||s.size()>32)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
    return true;
}

bool valid_email(std::string_view s){
    if(s.size()<3||s.size()>254)return false;
    const auto at=s.find('@');
    return at!=std::string_view::npos&&at>0&&at+1<s.size()&&s.find('@',at+1)==std::string_view::npos;
}

bool valid_display_name(std::string_view s){
    return !s.empty()&&s.size()<=64&&luma::contracts::auth::wire::valid_field(s);
}

bool valid_avatar_url(std::string_view s){
    return s.size()<=2048&&luma::contracts::auth::wire::valid_field(s);
}

std::string normalize(std::string s){
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}

std::string make_id(){return hex(random_bytes(16));}
std::string make_token(){return hex(random_bytes(32));}
}

class AuthServiceImpl final:public IAuthService{
public:
    Result Start()override{return StartOnPort(9100);}

    Result StartOnPort(std::uint16_t port)override{
        std::lock_guard lock(lifecycle_mutex_);
        if(running_)return Result::Failure(ErrorCode::InvalidState,"auth server already running");
        if(!port||!init_sockets())return Result::Failure(ErrorCode::InvalidArgument,"invalid auth port");
        if(!Load())return Result::Failure(ErrorCode::Internal,"unable to load auth store: "+store_path_);

        Socket s=::socket(AF_INET,SOCK_STREAM,0);
        if(s==kInvalidSocket)return Result::Failure(ErrorCode::Internal,"socket creation failed");
        int yes=1;
        setsockopt(s,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&yes),sizeof(yes));
        sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);a.sin_port=htons(port);
        if(::bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||::listen(s,32)!=0){
            close_socket(s);
            return Result::Failure(ErrorCode::Internal,"unable to bind auth port");
        }
        listen_socket_=s;port_=port;running_=true;
        accept_thread_=std::thread(&AuthServiceImpl::AcceptLoop,this);
        return Result::Ok();
    }

    Result Stop()override{
        std::lock_guard lock(lifecycle_mutex_);
        if(!running_&&!accept_thread_.joinable())return Result::Ok();

        running_=false;
        Socket s=listen_socket_.exchange(kInvalidSocket);
        if(s!=kInvalidSocket){
#ifdef _WIN32
            ::shutdown(s,SD_BOTH);
#else
            ::shutdown(s,SHUT_RDWR);
#endif
            close_socket(s);
        }
        if(accept_thread_.joinable())accept_thread_.join();

        {
            std::lock_guard cl(clients_mutex_);
            for(auto&[_,c]:clients_){
#ifdef _WIN32
                ::shutdown(c.socket,SD_BOTH);
#else
                ::shutdown(c.socket,SHUT_RDWR);
#endif
            }
        }
        for(auto& t:client_threads_)if(t.joinable())t.join();
        client_threads_.clear();

        {
            std::lock_guard cl(clients_mutex_);
            clients_.clear();
        }
        {
            std::lock_guard sl(session_mutex_);
            sessions_.clear();
        }
        return Result::Ok();
    }

    bool IsRunning()const override{return running_.load();}
    std::uint16_t Port()const override{return port_;}

    Result ConfigureStore(std::string path)override{
        if(path.empty())return Result::Failure(ErrorCode::InvalidArgument,"store path is empty");
        std::lock_guard lock(store_mutex_);
        if(running_)return Result::Failure(ErrorCode::InvalidState,"configure store before start");
        store_path_=std::move(path);
        return Result::Ok();
    }

private:
    bool Load(){
        std::lock_guard lock(store_mutex_);
        users_.clear();
        by_username_.clear();
        by_email_.clear();

        std::ifstream in(store_path_);
        if(!in)return true;

        std::string line;
        while(std::getline(in,line)){
            if(line.empty())continue;

            std::vector<std::string>p;
            std::string cur;
            for(char c:line){
                if(c=='\t'){p.push_back(std::move(cur));cur.clear();}
                else cur.push_back(c);
            }
            p.push_back(std::move(cur));

            if((p.size()!=7&&p.size()!=8)||p[0]!="1")return false;

            UserRecord u{};
            u.id=p[1];
            u.username=p[2];
            u.email=p[3];
            u.display_name=p[4];
            if(p.size()==7){
                u.avatar_url.clear();
                u.salt_hex=p[5];
                u.verifier_hex=p[6];
            }else{
                u.avatar_url=p[5];
                u.salt_hex=p[6];
                u.verifier_hex=p[7];
            }

            users_[u.id]=u;
            by_username_[normalize(u.username)]=u.id;
            by_email_[normalize(u.email)]=u.id;
        }
        return true;
    }

    bool SaveUnlocked(){
        std::filesystem::path tmp=store_path_+".tmp";
        std::filesystem::path backup=store_path_+".bak";
        std::error_code ec;

        const auto parent=std::filesystem::path(store_path_).parent_path();
        if(!parent.empty()){
            std::filesystem::create_directories(parent,ec);
            if(ec)return false;
        }

        std::filesystem::remove(tmp,ec);
        std::ofstream out(tmp,std::ios::trunc);
        if(!out)return false;

        for(const auto&[_,u]:users_){
            out<<"1\t"<<u.id
               <<"\t"<<u.username
               <<"\t"<<u.email
               <<"\t"<<u.display_name
               <<"\t"<<u.avatar_url
               <<"\t"<<u.salt_hex
               <<"\t"<<u.verifier_hex<<"\n";
        }
        out.close();
        if(!out){
            std::filesystem::remove(tmp,ec);
            return false;
        }

        std::filesystem::remove(backup,ec);
        ec.clear();
        const bool had_existing=std::filesystem::exists(store_path_,ec);
        if(ec)return false;

        if(had_existing){
            ec.clear();
            std::filesystem::rename(store_path_,backup,ec);
            if(ec){
                std::filesystem::remove(tmp,ec);
                return false;
            }
        }

        ec.clear();
        std::filesystem::rename(tmp,store_path_,ec);
        if(ec){
            std::filesystem::remove(tmp,ec);
            if(had_existing){
                std::error_code restore_ec;
                std::filesystem::rename(backup,store_path_,restore_ec);
            }
            return false;
        }

        std::filesystem::remove(backup,ec);
        return true;
    }

    bool Send(Socket s,const Packet&p){
        auto msg=luma::contracts::auth::wire::encode(p);
        std::size_t off=0;
        while(off<msg.size()){
            int n=::send(s,msg.data()+off,static_cast<int>(msg.size()-off),0);
            if(n<=0)return false;
            off+=static_cast<std::size_t>(n);
        }
        return true;
    }

    bool RecvLine(Socket s,std::string&line){
        line.clear();
        char c=0;
        while(running_){
            const int n=::recv(s,&c,1,0);
            if(n<=0)return false;
            if(c=='\n')return true;
            line.push_back(c);
            if(line.size()>=64*1024)return false;
        }
        return false;
    }

    void AcceptLoop(){
        while(running_){
            sockaddr_in a{};
#ifdef _WIN32
            int n=sizeof(a);
#else
            socklen_t n=sizeof(a);
#endif
            Socket listen=listen_socket_.load();
            if(listen==kInvalidSocket)break;

            Socket s=::accept(listen,reinterpret_cast<sockaddr*>(&a),&n);
            if(s==kInvalidSocket){
                if(running_)continue;
                break;
            }

            {
                std::lock_guard lock(clients_mutex_);
                clients_.emplace(s,ClientState{s,{},{}}); 
            }
            client_threads_.emplace_back(&AuthServiceImpl::ClientLoop,this,s);
        }
    }

    void ClientLoop(Socket s){
        while(running_){
            std::string line;
            if(!RecvLine(s,line))break;
            if(line.empty())continue;
            try{
                auto packet=luma::contracts::auth::wire::decode_line(line);
                Handle(s,packet);
            }catch(const std::exception& e){
                Send(s,{Type::Error,{"invalid_packet",e.what()}});
                break;
            }
        }

        {
            std::lock_guard lock(clients_mutex_);
            clients_.erase(s);
        }
        close_socket(s);
    }

    ClientState& Client(Socket s){
        auto it=clients_.find(s);
        if(it==clients_.end())throw std::runtime_error("unknown client");
        return it->second;
    }

    void Handle(Socket s,const Packet&p){
        auto send=[&](Type t,std::vector<std::string>f={}){return Send(s,{t,std::move(f)});};
        auto bad=[&](const char*c,const char*m){send(Type::Error,{c,m});};

        std::lock_guard client_lock(clients_mutex_);
        auto&c=Client(s);

        auto require_session=[&]() -> std::string {
            if(c.token.empty())return {};
            std::lock_guard sl(session_mutex_);
            auto it=sessions_.find(c.token);
            if(it==sessions_.end()||it->second.expires<=now_epoch()){
                if(it!=sessions_.end())sessions_.erase(it);
                return {};
            }
            return it->second.user_id;
        };

        auto session_expiry=[&]() -> std::int64_t {
            if(c.token.empty())return 0;
            std::lock_guard sl(session_mutex_);
            const auto it=sessions_.find(c.token);
            return it==sessions_.end()?0:it->second.expires;
        };

        switch(p.type){
        case Type::RegisterBegin:
            if(p.fields.size()!=3||!valid_username(p.fields[0])||!valid_email(p.fields[1])||!valid_display_name(p.fields[2])){
                bad("invalid_registration","invalid registration fields");return;
            }
            {
                std::lock_guard lock(store_mutex_);
                if(by_username_.count(normalize(p.fields[0]))||by_email_.count(normalize(p.fields[1]))){
                    bad("already_exists","username or email already exists");return;
                }
            }
            c.pending=Pending{
                Pending::Kind::Register,p.fields[0],normalize(p.fields[1]),p.fields[2],
                "",hex(random_bytes(16)),hex(random_bytes(32))};
            send(Type::RegisterChallenge,{c.pending.salt_hex,c.pending.nonce_hex});
            return;

        case Type::RegisterFinish:
            if(c.pending.kind!=Pending::Kind::Register||p.fields.size()!=1){
                bad("invalid_state","registration challenge missing");return;
            }
            {
                std::lock_guard lock(store_mutex_);
                if(by_username_.count(normalize(c.pending.username))||by_email_.count(normalize(c.pending.email))){
                    c.pending={};
                    bad("already_exists","username or email already exists");
                    return;
                }

                UserRecord u{};
                u.id=make_id();
                u.username=c.pending.username;
                u.email=c.pending.email;
                u.display_name=c.pending.display_name;
                u.avatar_url.clear();
                u.salt_hex=c.pending.salt_hex;
                u.verifier_hex=p.fields[0];

                users_[u.id]=u;
                by_username_[normalize(u.username)]=u.id;
                by_email_[normalize(u.email)]=u.id;

                if(!SaveUnlocked()){
                    users_.erase(u.id);
                    by_username_.erase(normalize(u.username));
                    by_email_.erase(normalize(u.email));
                    bad("storage_error","unable to persist account");
                    return;
                }

                c.pending={};
                send(Type::RegisterOk,{u.id,u.username,u.email,u.display_name});
            }
            return;

        case Type::LoginBegin:
            if(p.fields.size()!=1||p.fields[0].empty()||p.fields[0].size()>254){
                bad("invalid_identifier","invalid login identifier");return;
            }
            {
                std::lock_guard lock(store_mutex_);
                std::string id;
                auto it=by_username_.find(normalize(p.fields[0]));
                if(it!=by_username_.end())id=it->second;
                else{
                    auto ie=by_email_.find(normalize(p.fields[0]));
                    if(ie!=by_email_.end())id=ie->second;
                }
                if(id.empty()){
                    bad("invalid_credentials","invalid username or password");
                    return;
                }

                const auto&u=users_.at(id);
                c.pending=Pending{
                    Pending::Kind::Login,"","","",u.id,u.salt_hex,hex(random_bytes(32))};
                send(Type::LoginChallenge,{u.id,u.display_name,u.email,u.salt_hex,c.pending.nonce_hex});
            }
            return;

        case Type::LoginProof:
            if(c.pending.kind!=Pending::Kind::Login||p.fields.size()!=1){
                bad("invalid_state","login challenge missing");return;
            }
            {
                std::lock_guard lock(store_mutex_);
                const auto it=users_.find(c.pending.user_id);
                if(it==users_.end()){
                    c.pending={};
                    bad("invalid_credentials","invalid username or password");
                    return;
                }

                const auto verifier=from_hex(it->second.verifier_hex);
                const auto nonce=from_hex(c.pending.nonce_hex);
                const auto supplied=from_hex(p.fields[0]);
                const auto expected=hmac_sha256(verifier,nonce);

                if(!constant_time_equal(expected,supplied)){
                    c.pending={};
                    bad("invalid_credentials","invalid username or password");
                    return;
                }

                c.pending={};
                c.token=make_token();
                const auto expires=now_epoch()+24*60*60;
                {
                    std::lock_guard sl(session_mutex_);
                    sessions_[c.token]=SessionRecord{it->second.id,expires};
                }
                send(Type::LoginOk,{
                    c.token,it->second.id,it->second.username,it->second.email,
                    it->second.display_name,std::to_string(expires)});
            }
            return;

        case Type::Logout:
            if(p.fields.size()!=1||c.token.empty()||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            {
                std::lock_guard sl(session_mutex_);
                sessions_.erase(c.token);
            }
            c.token.clear();
            send(Type::LogoutOk);
            return;

        case Type::ValidateSession:
            if(p.fields.size()!=1||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            {
                const auto user_id=require_session();
                const auto expires=session_expiry();
                if(user_id.empty()||expires==0){
                    bad("invalid_session","session expired or invalid");
                    return;
                }
                std::lock_guard lock(store_mutex_);
                const auto it=users_.find(user_id);
                if(it==users_.end()){
                    bad("invalid_session","account no longer exists");
                    return;
                }
                send(Type::LoginOk,{
                    c.token,it->second.id,it->second.username,it->second.email,
                    it->second.display_name,std::to_string(expires)});
            }
            return;

        case Type::GetProfile: {
            if(p.fields.size()!=1||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            const auto user_id=require_session();
            if(user_id.empty()){
                bad("invalid_session","session expired or invalid");
                return;
            }
            std::lock_guard lock(store_mutex_);
            const auto it=users_.find(user_id);
            if(it==users_.end()){
                bad("invalid_session","account no longer exists");
                return;
            }
            const auto&u=it->second;
            send(Type::ProfileOk,{u.id,u.username,u.email,u.display_name,u.avatar_url});
            return;
        }

        case Type::UpdateProfile: {
            if(p.fields.size()!=5||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            if(!valid_username(p.fields[1])||!valid_email(p.fields[2])||
               !valid_display_name(p.fields[3])||!valid_avatar_url(p.fields[4])){
                bad("invalid_profile","invalid profile fields");
                return;
            }

            const auto user_id=require_session();
            if(user_id.empty()){
                bad("invalid_session","session expired or invalid");
                return;
            }

            std::lock_guard lock(store_mutex_);
            auto user_it=users_.find(user_id);
            if(user_it==users_.end()){
                bad("invalid_session","account no longer exists");
                return;
            }

            const auto old_username=normalize(user_it->second.username);
            const auto old_email=normalize(user_it->second.email);
            const auto new_username=normalize(p.fields[1]);
            const auto new_email=normalize(p.fields[2]);

            const auto username_it=by_username_.find(new_username);
            if(username_it!=by_username_.end()&&username_it->second!=user_id){
                bad("already_exists","username already exists");
                return;
            }

            const auto email_it=by_email_.find(new_email);
            if(email_it!=by_email_.end()&&email_it->second!=user_id){
                bad("already_exists","email already exists");
                return;
            }

            const UserRecord old=user_it->second;
            user_it->second.username=p.fields[1];
            user_it->second.email=p.fields[2];
            user_it->second.display_name=p.fields[3];
            user_it->second.avatar_url=p.fields[4];

            by_username_.erase(old_username);
            by_email_.erase(old_email);
            by_username_[new_username]=user_id;
            by_email_[new_email]=user_id;

            if(!SaveUnlocked()){
                user_it->second=old;
                by_username_.erase(new_username);
                by_email_.erase(new_email);
                by_username_[old_username]=user_id;
                by_email_[old_email]=user_id;
                bad("storage_error","unable to persist profile changes");
                return;
            }

            const auto&u=user_it->second;
            send(Type::UpdateProfileOk,{u.id,u.username,u.email,u.display_name,u.avatar_url});
            return;
        }

        case Type::DeleteAccount: {
            if(p.fields.size()!=1||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }

            const auto user_id=require_session();
            if(user_id.empty()){
                bad("invalid_session","session expired or invalid");
                return;
            }

            std::lock_guard lock(store_mutex_);
            auto user_it=users_.find(user_id);
            if(user_it==users_.end()){
                bad("invalid_session","account no longer exists");
                return;
            }

            const UserRecord old=user_it->second;
            const auto username=normalize(old.username);
            const auto email=normalize(old.email);

            users_.erase(user_it);
            by_username_.erase(username);
            by_email_.erase(email);

            if(!SaveUnlocked()){
                users_[old.id]=old;
                by_username_[username]=old.id;
                by_email_[email]=old.id;
                bad("storage_error","unable to persist account deletion");
                return;
            }

            {
                std::lock_guard sl(session_mutex_);
                for(auto it=sessions_.begin();it!=sessions_.end();){
                    if(it->second.user_id==user_id)it=sessions_.erase(it);
                    else ++it;
                }
            }

            c.token.clear();
            send(Type::DeleteAccountOk);
            return;
        }

        case Type::Ping:
            send(Type::Pong);
            return;

        case Type::RegisterChallenge:
        case Type::RegisterOk:
        case Type::LoginChallenge:
        case Type::LoginOk:
        case Type::LogoutOk:
        case Type::ProfileOk:
        case Type::UpdateProfileOk:
        case Type::DeleteAccountOk:
        case Type::Error:
        case Type::Pong:
            bad("unexpected_packet","unexpected authentication packet");
            return;

        default:
            bad("unsupported","unsupported auth operation");
            return;
        }
    }

    std::string store_path_{"./luma_auth_users.db"};
    std::atomic<bool>running_{false};
    std::atomic<Socket>listen_socket_{kInvalidSocket};
    std::uint16_t port_{0};
    std::thread accept_thread_;
    std::vector<std::thread>client_threads_;
    mutable std::mutex lifecycle_mutex_,clients_mutex_,store_mutex_,session_mutex_;
    std::unordered_map<Socket,ClientState>clients_;
    std::unordered_map<std::string,SessionRecord>sessions_;
    std::unordered_map<std::string,UserRecord>users_;
    std::unordered_map<std::string,std::string>by_username_,by_email_;
};

std::unique_ptr<IAuthService>CreateAuthService(){return std::make_unique<AuthServiceImpl>();}

}
