#include "IAuthService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include "contracts/auth/AuthWire.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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
using shared::contracts::ErrorCode;
using shared::contracts::Result;
using luma::contracts::auth::crypto::constant_time_equal;
using luma::contracts::auth::crypto::from_hex;
using luma::contracts::auth::crypto::hex;
using luma::contracts::auth::crypto::hmac_sha256;
using luma::contracts::auth::crypto::pbkdf2_hmac_sha256;
using luma::contracts::auth::crypto::random_bytes;
using luma::contracts::auth::crypto::sha256;
using luma::contracts::auth::wire::Packet;
using luma::contracts::auth::wire::Type;

namespace {

std::int64_t now_epoch(){
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string normalize(std::string s){
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}

std::string make_id(){return hex(random_bytes(16));}
std::string make_token(){return hex(random_bytes(32));}

std::string sha256_text(const std::string& value){
    const auto bytes=std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()),value.size());
    return hex(sha256(bytes));
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

bool valid_text(std::string_view s,std::size_t max){
    return !s.empty()&&s.size()<=max&&luma::contracts::auth::wire::valid_field(s);
}

std::string network_key(const std::string& remote,const std::string& identifier){
    return normalize(identifier)+"|"+remote;
}

struct UserRecord{
    std::string id,username,email,display_name,avatar_url,salt_hex,verifier_hex;
    bool email_verified{false};
    bool mfa_enabled{false};
    std::string mfa_recovery_hash;
    std::string email_verify_hash;
    std::int64_t email_verify_expires{0};
    std::string reset_token_hash;
    std::int64_t reset_token_expires{0};
};

struct Pending{
    enum class Kind{None,Register,Login,PasswordChange}kind{Kind::None};
    std::string username,email,display_name,user_id,salt_hex,nonce_hex,new_salt_hex;
};

struct ClientState{
    Socket socket{kInvalidSocket};
    Pending pending;
    std::string token;
    std::string device_id{"default-device"};
    std::string device_name{"LumaLive Client"};
    std::string remote_address{"unknown"};
};

struct SessionRecord{
    std::string session_id;
    std::string user_id;
    std::string device_id;
    std::string device_name;
    std::string remote_address;
    std::int64_t created{0};
    std::int64_t last_seen{0};
    std::int64_t expires{0};
};

struct FailureState{
    std::int64_t window_started{0};
    std::uint32_t count{0};
    std::int64_t locked_until{0};
};

struct AuditRecord{
    luma::contracts::auth::SecurityEvent event;
    std::string user_id;
};

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
        sockaddr_in a{};
        a.sin_family=AF_INET;
        a.sin_addr.s_addr=htonl(INADDR_ANY);
        a.sin_port=htons(port);

        if(::bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||::listen(s,32)!=0){
            close_socket(s);
            return Result::Failure(ErrorCode::Internal,"unable to bind auth port");
        }

        listen_socket_=s;
        port_=port;
        running_=true;
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
        {
            std::lock_guard lock(store_mutex_);
            users_.clear();
            by_username_.clear();
            by_email_.clear();

            std::ifstream in(store_path_);
            if(in){
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

                    if((p.size()!=7&&p.size()!=8&&p.size()!=15)||p[0]!="1")return false;

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
                        if(p.size()==15){
                            u.email_verified=p[8]=="1";
                            u.mfa_enabled=p[9]=="1";
                            u.mfa_recovery_hash=p[10];
                            u.email_verify_hash=p[11];
                            u.email_verify_expires=std::stoll(p[12]);
                            u.reset_token_hash=p[13];
                            u.reset_token_expires=std::stoll(p[14]);
                        }
                    }

                    users_[u.id]=u;
                    by_username_[normalize(u.username)]=u.id;
                    by_email_[normalize(u.email)]=u.id;
                }
            }
        }

        std::lock_guard lock(audit_mutex_);
        audits_.clear();
        std::ifstream audit(store_path_+".security.log");
        if(audit){
            std::string line;
            while(std::getline(audit,line)){
                std::vector<std::string>p;
                std::string cur;
                for(char c:line){
                    if(c=='\t'){p.push_back(std::move(cur));cur.clear();}
                    else cur.push_back(c);
                }
                p.push_back(std::move(cur));
                if(p.size()!=5)continue;
                AuditRecord a{};
                a.user_id=p[1];
                a.event.event_id=p[0];
                a.event.created_at_epoch_seconds=std::stoll(p[2]);
                a.event.type=p[3];
                a.event.detail=p[4];
                audits_.push_back(std::move(a));
                if(audits_.size()>512)audits_.erase(audits_.begin());
            }
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
               <<"\t"<<u.verifier_hex
               <<"\t"<<(u.email_verified?"1":"0")
               <<"\t"<<(u.mfa_enabled?"1":"0")
               <<"\t"<<u.mfa_recovery_hash
               <<"\t"<<u.email_verify_hash
               <<"\t"<<u.email_verify_expires
               <<"\t"<<u.reset_token_hash
               <<"\t"<<u.reset_token_expires<<"\n";
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

    bool Send(Socket s,const Packet& p){
        const auto msg=luma::contracts::auth::wire::encode(p);
        std::size_t off=0;
        while(off<msg.size()){
            const int n=::send(s,msg.data()+off,static_cast<int>(msg.size()-off),0);
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

    std::string RemoteAddress(const sockaddr_in& a){
        char host[INET_ADDRSTRLEN]{};
#ifdef _WIN32
        if(::InetNtopA(AF_INET,&a.sin_addr,host,sizeof(host))==nullptr)return "unknown";
#else
        if(::inet_ntop(AF_INET,&a.sin_addr,host,sizeof(host))==nullptr)return "unknown";
#endif
        return host;
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

            ClientState state{};
            state.socket=s;
            state.remote_address=RemoteAddress(a);
            {
                std::lock_guard lock(clients_mutex_);
                clients_.emplace(s,std::move(state));
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
                const auto packet=luma::contracts::auth::wire::decode_line(line);
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

    std::string SessionUser(const ClientState& c){
        if(c.token.empty())return {};
        std::lock_guard sl(session_mutex_);
        auto it=sessions_.find(c.token);
        if(it==sessions_.end()||it->second.expires<=now_epoch()){
            if(it!=sessions_.end())sessions_.erase(it);
            return {};
        }
        it->second.last_seen=now_epoch();
        return it->second.user_id;
    }

    std::int64_t SessionExpiry(const ClientState& c){
        std::lock_guard sl(session_mutex_);
        const auto it=sessions_.find(c.token);
        return it==sessions_.end()?0:it->second.expires;
    }

    void AppendAudit(const std::string& user_id,const std::string& type,const std::string& detail){
        AuditRecord record{};
        record.user_id=user_id;
        record.event.event_id=make_id();
        record.event.type=type;
        record.event.detail=detail;
        record.event.created_at_epoch_seconds=now_epoch();

        {
            std::lock_guard lock(audit_mutex_);
            audits_.push_back(record);
            if(audits_.size()>512)audits_.erase(audits_.begin());
        }

        std::ofstream out(store_path_+".security.log",std::ios::app);
        if(out){
            out<<record.event.event_id<<"\t"<<record.user_id<<"\t"
               <<record.event.created_at_epoch_seconds<<"\t"
               <<record.event.type<<"\t"<<record.event.detail<<"\n";
        }
    }

    bool IsLocked(const std::string& key,std::int64_t now){
        std::lock_guard lock(rate_mutex_);
        auto it=failures_.find(key);
        if(it==failures_.end())return false;
        if(it->second.locked_until>now)return true;
        if(it->second.locked_until!=0)it->second={};
        return false;
    }

    void RecordLoginFailure(const std::string& key,const std::string& user_id){
        const auto now=now_epoch();
        bool locked=false;
        {
            std::lock_guard lock(rate_mutex_);
            auto&state=failures_[key];
            if(state.window_started==0||now-state.window_started>900){
                state.window_started=now;
                state.count=0;
                state.locked_until=0;
            }
            ++state.count;
            if(state.count>=5){
                state.locked_until=now+900;
                locked=true;
            }
        }
        AppendAudit(user_id,"login_failure",locked?"account temporarily locked":"invalid credentials");
    }

    void ClearLoginFailures(const std::string& identifier){
        std::lock_guard lock(rate_mutex_);
        failures_.erase(normalize(identifier));
    }

    bool TooManyResetRequests(const std::string& identifier){
        const auto key="reset|"+normalize(identifier);
        const auto now=now_epoch();
        std::lock_guard lock(rate_mutex_);
        auto&state=failures_[key];
        if(state.window_started==0||now-state.window_started>600){
            state.window_started=now;
            state.count=0;
            state.locked_until=0;
        }
        if(state.count>=3)return true;
        ++state.count;
        return false;
    }

    void Handle(Socket s,const Packet&p){
        auto send=[&](Type t,std::vector<std::string>f={}){return Send(s,{t,std::move(f)});};
        auto bad=[&](const char*c,const char*m){send(Type::Error,{c,m});};

        std::lock_guard client_lock(clients_mutex_);
        auto&c=Client(s);

        auto require_session=[&]() -> std::string {
            return SessionUser(c);
        };

        switch(p.type){
        case Type::RegisterBegin: {
            if(p.fields.size()!=3||!valid_username(p.fields[0])||!valid_email(p.fields[1])||!valid_text(p.fields[2],64)){
                bad("invalid_registration","invalid registration fields");return;
            }
            std::lock_guard lock(store_mutex_);
            if(by_username_.count(normalize(p.fields[0]))||by_email_.count(normalize(p.fields[1]))){
                bad("already_exists","username or email already exists");return;
            }
            c.pending=Pending{Pending::Kind::Register,p.fields[0],normalize(p.fields[1]),p.fields[2],"","","", ""};
            c.pending.salt_hex=hex(random_bytes(16));
            c.pending.nonce_hex=hex(random_bytes(32));
            send(Type::RegisterChallenge,{c.pending.salt_hex,c.pending.nonce_hex});
            return;
        }

        case Type::RegisterFinish: {
            if(c.pending.kind!=Pending::Kind::Register||p.fields.size()!=1){
                bad("invalid_state","registration challenge missing");return;
            }
            std::lock_guard lock(store_mutex_);
            if(by_username_.count(normalize(c.pending.username))||by_email_.count(normalize(c.pending.email))){
                c.pending={};
                bad("already_exists","username or email already exists");return;
            }
            UserRecord u{};
            u.id=make_id();
            u.username=c.pending.username;
            u.email=c.pending.email;
            u.display_name=c.pending.display_name;
            u.salt_hex=c.pending.salt_hex;
            u.verifier_hex=p.fields[0];

            users_[u.id]=u;
            by_username_[normalize(u.username)]=u.id;
            by_email_[normalize(u.email)]=u.id;

            if(!SaveUnlocked()){
                users_.erase(u.id);
                by_username_.erase(normalize(u.username));
                by_email_.erase(normalize(u.email));
                bad("storage_error","unable to persist account");return;
            }

            c.pending={};
            AppendAudit(u.id,"account_registered","account created");
            send(Type::RegisterOk,{u.id,u.username,u.email,u.display_name});
            return;
        }

        case Type::LoginBegin: {
            if(p.fields.size()!=3||p.fields[0].empty()||p.fields[0].size()>254||
               !valid_text(p.fields[1],128)||!valid_text(p.fields[2],128)){
                bad("invalid_identifier","invalid login information");return;
            }

            const auto identifier_key=normalize(p.fields[0]);
            if(IsLocked(identifier_key,now_epoch())){
                bad("temporarily_locked","too many failed login attempts; try again later");return;
            }

            std::lock_guard lock(store_mutex_);
            std::string user_id;
            auto it=by_username_.find(identifier_key);
            if(it!=by_username_.end())user_id=it->second;
            else{
                auto ie=by_email_.find(identifier_key);
                if(ie!=by_email_.end())user_id=ie->second;
            }
            if(user_id.empty()){
                RecordLoginFailure(identifier_key,{});
                bad("invalid_credentials","invalid username or password");return;
            }

            const auto&u=users_.at(user_id);
            c.device_id=p.fields[1];
            c.device_name=p.fields[2];
            c.pending=Pending{Pending::Kind::Login,"","","",u.id,u.salt_hex,hex(random_bytes(32)),""};
            send(Type::LoginChallenge,{
                u.id,u.display_name,u.email,u.salt_hex,c.pending.nonce_hex,u.mfa_enabled?"1":"0"});
            return;
        }

        case Type::LoginProof: {
            if(c.pending.kind!=Pending::Kind::Login||p.fields.size()!=2){
                bad("invalid_state","login challenge missing");return;
            }
            const auto user_id=c.pending.user_id;
            std::lock_guard lock(store_mutex_);
            const auto it=users_.find(user_id);
            if(it==users_.end()){
                c.pending={};
                bad("invalid_credentials","invalid username or password");return;
            }

            try{
                const auto verifier=from_hex(it->second.verifier_hex);
                const auto nonce=from_hex(c.pending.nonce_hex);
                const auto supplied=from_hex(p.fields[0]);
                const auto expected=hmac_sha256(verifier,nonce);

                bool proof_ok=constant_time_equal(expected,supplied);
                bool mfa_ok=true;
                if(it->second.mfa_enabled){
                    mfa_ok=!it->second.mfa_recovery_hash.empty()&&
                        constant_time_equal(
                            from_hex(it->second.mfa_recovery_hash),
                            from_hex(sha256_text(p.fields[1])));
                }

                if(!proof_ok||!mfa_ok){
                    c.pending={};
                    RecordLoginFailure(normalize(it->second.username),user_id);
                    bad("invalid_credentials","invalid username or password");return;
                }
            }catch(...){
                c.pending={};
                RecordLoginFailure(normalize(it->second.username),user_id);
                bad("invalid_credentials","invalid username or password");return;
            }

            ClearLoginFailures(normalize(it->second.username));
            ClearLoginFailures(normalize(it->second.email));

            const auto session_id=make_id();
            const auto token=make_token();
            const auto now=now_epoch();
            const auto expires=now+24*60*60;

            bool new_network=true;
            {
                std::lock_guard sl(session_mutex_);
                for(auto&[_,session]:sessions_){
                    if(session.user_id==user_id&&session.remote_address==c.remote_address){
                        new_network=false;
                        break;
                    }
                }
                sessions_[token]=SessionRecord{
                    session_id,user_id,c.device_id,c.device_name,c.remote_address,now,now,expires};
            }

            c.pending={};
            c.token=token;

            AppendAudit(user_id,"login_success","login succeeded");
            if(new_network)AppendAudit(user_id,"new_network","login from an unseen network address");
            send(Type::LoginOk,{
                token,session_id,c.device_id,c.device_name,it->second.id,
                it->second.username,it->second.email,it->second.display_name,
                std::to_string(expires)});
            return;
        }

        case Type::Logout:
            if(p.fields.size()!=1||c.token.empty()||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            {
                std::lock_guard sl(session_mutex_);
                sessions_.erase(c.token);
            }
            AppendAudit(require_session(),"logout","current session signed out");
            c.token.clear();
            send(Type::LogoutOk);
            return;

        case Type::ValidateSession: {
            if(p.fields.size()!=1||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            const auto user_id=require_session();
            const auto expires=SessionExpiry(c);
            if(user_id.empty()||expires==0){
                bad("invalid_session","session expired or invalid");return;
            }
            std::lock_guard lock(store_mutex_);
            const auto it=users_.find(user_id);
            if(it==users_.end()){
                bad("invalid_session","account no longer exists");return;
            }
            send(Type::LoginOk,{
                c.token,
                sessions_.at(c.token).session_id,
                sessions_.at(c.token).device_id,
                sessions_.at(c.token).device_name,
                it->second.id,it->second.username,it->second.email,it->second.display_name,
                std::to_string(expires)});
            return;
        }

        case Type::GetProfile:
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::lock_guard lock(store_mutex_);
                const auto it=users_.find(user_id);
                if(it==users_.end()){bad("invalid_session","account no longer exists");return;}
                send(Type::ProfileOk,{it->second.id,it->second.username,it->second.email,it->second.display_name,it->second.avatar_url});
            }
            return;

        case Type::UpdateProfile:
            if(p.fields.size()!=5||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            if(!valid_username(p.fields[1])||!valid_email(p.fields[2])||!valid_text(p.fields[3],64)||
               p.fields[4].size()>2048||!luma::contracts::auth::wire::valid_field(p.fields[4])){
                bad("invalid_profile","invalid profile fields");return;
            }
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::lock_guard lock(store_mutex_);
                auto user_it=users_.find(user_id);
                if(user_it==users_.end()){bad("invalid_session","account no longer exists");return;}

                const auto old_username=normalize(user_it->second.username);
                const auto old_email=normalize(user_it->second.email);
                const auto new_username=normalize(p.fields[1]);
                const auto new_email=normalize(p.fields[2]);

                auto ui=by_username_.find(new_username);
                if(ui!=by_username_.end()&&ui->second!=user_id){bad("already_exists","username already exists");return;}
                auto ei=by_email_.find(new_email);
                if(ei!=by_email_.end()&&ei->second!=user_id){bad("already_exists","email already exists");return;}

                const auto old=user_it->second;
                const bool email_changed=old.email!=p.fields[2];
                user_it->second.username=p.fields[1];
                user_it->second.email=p.fields[2];
                user_it->second.display_name=p.fields[3];
                user_it->second.avatar_url=p.fields[4];
                if(email_changed){
                    user_it->second.email_verified=false;
                    user_it->second.email_verify_hash.clear();
                    user_it->second.email_verify_expires=0;
                }

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
                    bad("storage_error","unable to persist profile changes");return;
                }

                AppendAudit(user_id,email_changed?"profile_email_changed":"profile_updated",
                    email_changed?"email changed; verification required":"profile updated");
                const auto&u=user_it->second;
                send(Type::UpdateProfileOk,{u.id,u.username,u.email,u.display_name,u.avatar_url});
            }
            return;

        case Type::DeleteAccount:
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::lock_guard lock(store_mutex_);
                auto it=users_.find(user_id);
                if(it==users_.end()){bad("invalid_session","account no longer exists");return;}
                const auto old=it->second;
                const auto username=normalize(old.username);
                const auto email=normalize(old.email);
                users_.erase(it);
                by_username_.erase(username);
                by_email_.erase(email);

                if(!SaveUnlocked()){
                    users_[old.id]=old;
                    by_username_[username]=old.id;
                    by_email_[email]=old.id;
                    bad("storage_error","unable to persist account deletion");return;
                }

                {
                    std::lock_guard sl(session_mutex_);
                    for(auto sit=sessions_.begin();sit!=sessions_.end();){
                        if(sit->second.user_id==user_id)sit=sessions_.erase(sit);
                        else ++sit;
                    }
                }
                AppendAudit(user_id,"account_deleted","account permanently deleted");
                c.token.clear();
                send(Type::DeleteAccountOk);
            }
            return;

        case Type::RequestEmailVerification: {
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            const auto user_id=require_session();
            if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
            std::lock_guard lock(store_mutex_);
            auto it=users_.find(user_id);
            if(it==users_.end()){bad("invalid_session","account no longer exists");return;}
            if(it->second.email_verified){bad("already_verified","email is already verified");return;}

            const auto token=make_token();
            it->second.email_verify_hash=sha256_text(token);
            it->second.email_verify_expires=now_epoch()+30*60;
            if(!SaveUnlocked()){bad("storage_error","unable to persist verification challenge");return;}
            AppendAudit(user_id,"email_verification_requested","verification challenge issued");
            send(Type::EmailVerificationIssued,{token,std::to_string(it->second.email_verify_expires)});
            return;
        }

        case Type::VerifyEmail: {
            if(p.fields.size()!=2||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            const auto user_id=require_session();
            if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
            std::lock_guard lock(store_mutex_);
            auto it=users_.find(user_id);
            if(it==users_.end()){bad("invalid_session","account no longer exists");return;}
            if(it->second.email_verify_hash.empty()||it->second.email_verify_expires<now_epoch()){
                bad("verification_expired","email verification challenge expired");return;
            }
            if(sha256_text(p.fields[1])!=it->second.email_verify_hash){
                bad("invalid_token","invalid email verification token");return;
            }
            it->second.email_verified=true;
            it->second.email_verify_hash.clear();
            it->second.email_verify_expires=0;
            if(!SaveUnlocked()){bad("storage_error","unable to persist email verification");return;}
            AppendAudit(user_id,"email_verified","email address verified");
            send(Type::EmailVerified);
            return;
        }

        case Type::ChangePasswordBegin: {
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            const auto user_id=require_session();
            if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
            std::lock_guard lock(store_mutex_);
            const auto it=users_.find(user_id);
            if(it==users_.end()){bad("invalid_session","account no longer exists");return;}
            c.pending=Pending{Pending::Kind::PasswordChange,"","","",user_id,it->second.salt_hex,hex(random_bytes(32)),hex(random_bytes(16))};
            send(Type::ChangePasswordChallenge,{c.pending.salt_hex,c.pending.nonce_hex,c.pending.new_salt_hex});
            return;
        }

        case Type::ChangePasswordFinish: {
            if(c.pending.kind!=Pending::Kind::PasswordChange||p.fields.size()!=3||p.fields[0]!=c.token){
                bad("invalid_state","password change challenge missing");return;
            }
            const auto user_id=c.pending.user_id;
            std::lock_guard lock(store_mutex_);
            auto it=users_.find(user_id);
            if(it==users_.end()){bad("invalid_session","account no longer exists");return;}

            try{
                const auto current_verifier=from_hex(it->second.verifier_hex);
                const auto nonce=from_hex(c.pending.nonce_hex);
                const auto supplied=from_hex(p.fields[1]);
                const auto expected=hmac_sha256(current_verifier,nonce);
                if(!constant_time_equal(expected,supplied)){
                    c.pending={};
                    bad("invalid_credentials","current password is incorrect");return;
                }
            }catch(...){
                c.pending={};
                bad("invalid_credentials","current password is incorrect");return;
            }

            it->second.salt_hex=c.pending.new_salt_hex;
            it->second.verifier_hex=p.fields[2];
            it->second.reset_token_hash.clear();
            it->second.reset_token_expires=0;

            if(!SaveUnlocked()){bad("storage_error","unable to persist password change");return;}

            {
                std::lock_guard sl(session_mutex_);
                for(auto sit=sessions_.begin();sit!=sessions_.end();){
                    if(sit->second.user_id==user_id)sit=sessions_.erase(sit);
                    else ++sit;
                }
            }
            c.pending={};
            c.token.clear();
            AppendAudit(user_id,"password_changed","password changed; all sessions revoked");
            send(Type::PasswordChanged);
            return;
        }

        case Type::RequestPasswordReset: {
            if(p.fields.size()!=1||p.fields[0].empty()||p.fields[0].size()>254){
                bad("invalid_identifier","invalid reset identifier");return;
            }
            if(TooManyResetRequests(p.fields[0])){
                bad("temporarily_locked","too many reset requests; try again later");return;
            }

            std::lock_guard lock(store_mutex_);
            std::string user_id;
            const auto key=normalize(p.fields[0]);
            auto ui=by_username_.find(key);
            if(ui!=by_username_.end())user_id=ui->second;
            else{
                auto ei=by_email_.find(key);
                if(ei!=by_email_.end())user_id=ei->second;
            }
            if(user_id.empty()){
                bad("reset_unavailable","if the account exists, a reset message will be available through the configured delivery channel");
                return;
            }

            auto&u=users_.at(user_id);
            const auto token=make_token();
            const auto salt_hex=hex(random_bytes(16));
            u.reset_token_hash=sha256_text(token);
            u.reset_token_expires=now_epoch()+30*60;

            if(!SaveUnlocked()){bad("storage_error","unable to persist reset challenge");return;}
            AppendAudit(user_id,"password_reset_requested","password reset challenge issued");
            send(Type::PasswordResetIssued,{token,salt_hex,std::to_string(u.reset_token_expires)});
            return;
        }

        case Type::ResetPassword: {
            if(p.fields.size()!=3){
                bad("invalid_reset","reset token, salt and verifier are required");return;
            }
            std::lock_guard lock(store_mutex_);
            UserRecord* target=nullptr;
            for(auto&[_,u]:users_){
                if(u.reset_token_hash==sha256_text(p.fields[0])){
                    target=&u;break;
                }
            }
            if(!target||target->reset_token_expires<now_epoch()){
                bad("invalid_token","password reset token is invalid or expired");return;
            }

            target->salt_hex=p.fields[1];
            target->verifier_hex=p.fields[2];
            target->reset_token_hash.clear();
            target->reset_token_expires=0;

            if(!SaveUnlocked()){bad("storage_error","unable to persist password reset");return;}

            {
                std::lock_guard sl(session_mutex_);
                for(auto sit=sessions_.begin();sit!=sessions_.end();){
                    if(sit->second.user_id==target->id)sit=sessions_.erase(sit);
                    else ++sit;
                }
            }
            AppendAudit(target->id,"password_reset_completed","password reset completed; all sessions revoked");
            send(Type::PasswordResetOk);
            return;
        }

        case Type::GetSecuritySummary:
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::lock_guard lock(store_mutex_);
                auto it=users_.find(user_id);
                if(it==users_.end()){bad("invalid_session","account no longer exists");return;}

                std::uint32_t failed=0;
                {
                    std::lock_guard rl(rate_mutex_);
                    auto ui=failures_.find(normalize(it->second.username));
                    if(ui!=failures_.end())failed=ui->second.count;
                    auto ei=failures_.find(normalize(it->second.email));
                    if(ei!=failures_.end())failed=std::max(failed,ei->second.count);
                }

                std::uint32_t active=0;
                {
                    std::lock_guard sl(session_mutex_);
                    for(const auto&[_,s]:sessions_)if(s.user_id==user_id&&s.expires>now_epoch())++active;
                }
                send(Type::SecuritySummaryOk,{
                    it->second.email_verified?"1":"0",
                    it->second.mfa_enabled?"1":"0",
                    std::to_string(failed),
                    std::to_string(active)});
            }
            return;

        case Type::EnableMfa:
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::lock_guard lock(store_mutex_);
                auto it=users_.find(user_id);
                if(it==users_.end()){bad("invalid_session","account no longer exists");return;}
                if(it->second.mfa_enabled){bad("already_enabled","MFA is already enabled");return;}
                const auto recovery_code=hex(random_bytes(16));
                it->second.mfa_recovery_hash=sha256_text(recovery_code);
                it->second.mfa_enabled=true;
                if(!SaveUnlocked()){bad("storage_error","unable to persist MFA setting");return;}
                AppendAudit(user_id,"mfa_enabled","MFA enabled with a recovery code");
                send(Type::MfaEnabled,{recovery_code});
            }
            return;

        case Type::DisableMfa:
            if(p.fields.size()!=2||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::lock_guard lock(store_mutex_);
                auto it=users_.find(user_id);
                if(it==users_.end()){bad("invalid_session","account no longer exists");return;}
                if(!it->second.mfa_enabled){bad("not_enabled","MFA is not enabled");return;}
                if(it->second.mfa_recovery_hash.empty()||
                   sha256_text(p.fields[1])!=it->second.mfa_recovery_hash){
                    bad("invalid_recovery_code","invalid MFA recovery code");return;
                }
                it->second.mfa_enabled=false;
                it->second.mfa_recovery_hash.clear();
                if(!SaveUnlocked()){bad("storage_error","unable to persist MFA setting");return;}
                AppendAudit(user_id,"mfa_disabled","MFA disabled with recovery code");
                send(Type::MfaDisabled);
            }
            return;

        case Type::GetSessions:
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::lock_guard sl(session_mutex_);
                std::vector<std::string> fields;
                std::size_t count=0;
                for(const auto&[token,s]:sessions_){
                    if(s.user_id==user_id&&s.expires>now_epoch())++count;
                }
                fields.push_back(std::to_string(count));
                for(const auto&[token,s]:sessions_){
                    if(s.user_id!=user_id||s.expires<=now_epoch())continue;
                    fields.push_back(s.session_id);
                    fields.push_back(s.device_id);
                    fields.push_back(s.device_name);
                    fields.push_back(s.remote_address);
                    fields.push_back(std::to_string(s.created));
                    fields.push_back(std::to_string(s.last_seen));
                    fields.push_back(token==c.token?"1":"0");
                }
                send(Type::SessionsOk,std::move(fields));
            }
            return;

        case Type::RevokeSession:
            if(p.fields.size()!=2||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                bool found=false;
                std::string revoked_token;
                {
                    std::lock_guard sl(session_mutex_);
                    for(auto it=sessions_.begin();it!=sessions_.end();++it){
                        if(it->second.session_id==p.fields[1]&&it->second.user_id==user_id){
                            found=true;revoked_token=it->first;sessions_.erase(it);break;
                        }
                    }
                }
                if(!found){bad("not_found","session not found");return;}
                if(revoked_token==c.token)c.token.clear();
                AppendAudit(user_id,"session_revoked","one session revoked");
                send(Type::SessionRevoked);
            }
            return;

        case Type::RevokeOtherSessions:
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::uint32_t revoked=0;
                {
                    std::lock_guard sl(session_mutex_);
                    for(auto it=sessions_.begin();it!=sessions_.end();){
                        if(it->second.user_id==user_id&&it->first!=c.token){
                            it=sessions_.erase(it);++revoked;
                        }else ++it;
                    }
                }
                AppendAudit(user_id,"other_sessions_revoked","revoked "+std::to_string(revoked)+" other sessions");
                send(Type::SessionsRevoked,{std::to_string(revoked)});
            }
            return;

        case Type::GetSecurityEvents:
            if(p.fields.size()!=1||p.fields[0]!=c.token){bad("invalid_session","invalid session");return;}
            {
                const auto user_id=require_session();
                if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
                std::lock_guard al(audit_mutex_);
                std::vector<std::string> fields;
                std::vector<const AuditRecord*> own;
                for(auto it=audits_.rbegin();it!=audits_.rend()&&own.size()<100;++it){
                    if(it->user_id==user_id)own.push_back(&*it);
                }
                fields.push_back(std::to_string(own.size()));
                for(const auto*e:own){
                    fields.push_back(e->event.event_id);
                    fields.push_back(e->event.type);
                    fields.push_back(e->event.detail);
                    fields.push_back(std::to_string(e->event.created_at_epoch_seconds));
                }
                send(Type::SecurityEventsOk,std::move(fields));
            }
            return;

        case Type::Ping:
            send(Type::Pong);return;

        case Type::RegisterChallenge:
        case Type::RegisterOk:
        case Type::LoginChallenge:
        case Type::LoginOk:
        case Type::LogoutOk:
        case Type::ProfileOk:
        case Type::UpdateProfileOk:
        case Type::DeleteAccountOk:
        case Type::EmailVerificationIssued:
        case Type::EmailVerified:
        case Type::ChangePasswordChallenge:
        case Type::PasswordChanged:
        case Type::PasswordResetIssued:
        case Type::PasswordResetOk:
        case Type::SecuritySummaryOk:
        case Type::MfaEnabled:
        case Type::MfaDisabled:
        case Type::SessionsOk:
        case Type::SessionRevoked:
        case Type::SessionsRevoked:
        case Type::SecurityEventsOk:
        case Type::Error:
        case Type::Pong:
            bad("unexpected_packet","unexpected authentication packet");return;

        default:
            bad("unsupported","unsupported auth operation");return;
        }
    }

    std::string store_path_{"./luma_auth_users.db"};
    std::atomic<bool> running_{false};
    std::atomic<Socket> listen_socket_{kInvalidSocket};
    std::uint16_t port_{0};
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    mutable std::mutex lifecycle_mutex_,clients_mutex_,store_mutex_,session_mutex_,rate_mutex_,audit_mutex_;
    std::unordered_map<Socket,ClientState> clients_;
    std::unordered_map<std::string,SessionRecord> sessions_;
    std::unordered_map<std::string,UserRecord> users_;
    std::unordered_map<std::string,std::string> by_username_,by_email_;
    std::unordered_map<std::string,FailureState> failures_;
    std::vector<AuditRecord> audits_;
};

std::unique_ptr<IAuthService>CreateAuthService(){
    return std::make_unique<AuthServiceImpl>();
}

}
