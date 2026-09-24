#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "ISmsProvider.hpp"
#include "IEmailProvider.hpp"
#include "contracts/auth/Auth.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include "contracts/auth/AuthWire.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <span>
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
using luma::contracts::auth::crypto::base32_encode;
using luma::contracts::auth::crypto::make_otpauth_uri;
using luma::contracts::auth::crypto::verify_totp;
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

std::string normalize_phone(std::string_view input){
    std::string out;
    out.reserve(16);
    bool saw_plus=false;
    for(char c:input){
        if(c=='+'){
            if(saw_plus||!out.empty())return {};
            saw_plus=true;
            out.push_back('+');
        }else if(std::isdigit(static_cast<unsigned char>(c))){
            out.push_back(c);
        }else if(c==' '||c=='-'||c=='('||c==')'||c=='.'){
            continue;
        }else{
            return {};
        }
    }
    if(!saw_plus)out.insert(out.begin(),'+');
    const auto digits=out.size()>0?out.size()-1:0;
    if(digits<8||digits>15)return {};
    return out;
}

std::string make_otp_code(){
    const auto bytes=random_bytes(4);
    const std::uint32_t value=(std::uint32_t(bytes[0])<<24)|
        (std::uint32_t(bytes[1])<<16)|(std::uint32_t(bytes[2])<<8)|std::uint32_t(bytes[3]);
    std::ostringstream out;
    out<<std::setw(6)<<std::setfill('0')<<(value%1000000u);
    return out.str();
}

bool valid_text(std::string_view s,std::size_t max){
    return !s.empty()&&s.size()<=max&&luma::contracts::auth::wire::valid_field(s);
}

struct UserRecord{
    std::string id,username,email,display_name,avatar_url,phone,salt_hex,verifier_hex;
    bool email_verified{false};
    bool phone_verified{false};
    bool mfa_enabled{false};
    std::string mfa_recovery_hash;
    std::string mfa_totp_secret_hex;
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
    std::string refresh_token_hash;
    std::int64_t created{0};
    std::int64_t last_seen{0};
    std::int64_t expires{0};
    std::int64_t refresh_expires{0};
};

struct FailureState{
    std::int64_t window_started{0};
    std::uint32_t count{0};
    std::int64_t locked_until{0};
};

struct AuditRecord{
    luma::contracts::auth::SecurityEvent security_event;
    std::string user_id;
};

struct PhoneChallenge{
    enum class Kind{Verification,Login} kind{Kind::Login};
    std::string phone;
    std::string user_id;
    std::string code_hash;
    std::int64_t expires{0};
    std::uint32_t attempts_remaining{5};
};

}

class AuthServiceImpl final:public IAuthService{
public:
    explicit AuthServiceImpl(std::unique_ptr<IAuthStore> store={})
        :auth_store_(store?std::move(store):CreateAuthStoreFromEnvironment()),
         sms_provider_(CreateSmsProviderFromEnvironment()),
         email_provider_(CreateEmailProviderFromEnvironment()){}

    Result Start()override{return StartOnPort(9100);}

    Result StartOnPort(std::uint16_t port)override{
        std::lock_guard lock(lifecycle_mutex_);
        if(running_)return Result::Failure(ErrorCode::InvalidState,"auth server already running");
        if(!port||!init_sockets())return Result::Failure(ErrorCode::InvalidArgument,"invalid auth port");
        if(!auth_store_){
            return Result::Failure(ErrorCode::Internal,"auth database store is not configured");
        }
        if(auto db=auth_store_->Open();!db.IsOk())return db;
        if(!Load()){
            auth_store_->Close();
            return Result::Failure(ErrorCode::Internal,"unable to load auth database");
        }

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
            auth_store_->Close();
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
        if(auth_store_)auth_store_->Close();
        return Result::Ok();
    }

    bool IsRunning()const override{return running_.load();}
    std::uint16_t Port()const override{return port_;}

    Result ConfigureDatabase(std::string connection_string) override{
        if(connection_string.empty())return Result::Failure(
            ErrorCode::InvalidArgument,"database connection string is empty");
        std::lock_guard lock(lifecycle_mutex_);
        if(running_)return Result::Failure(
            ErrorCode::InvalidState,"configure database before start");
        auth_store_=CreatePostgresAuthStore(std::move(connection_string));
        return Result::Ok();
    }

private:
    EmailSendResult SendEmailToken(
        std::string_view email,
        std::string_view purpose,
        std::string_view token,
        std::chrono::seconds ttl){
        if(!email_provider_)return {false,{},{ },"email provider is not configured"};
        return email_provider_->SendToken(email,purpose,token,ttl);
    }

    SmsSendResult SendSmsOtp(
        std::string_view phone,
        std::string_view purpose,
        std::string_view code,
        std::chrono::seconds ttl){
        if(!sms_provider_)return {false,{},{},"SMS provider is not configured"};
        return sms_provider_->SendOtp(phone,purpose,code,ttl);
    }

    bool Load(){
        if(!auth_store_)return false;

        std::vector<AuthUserRecord> rows;
        if(!auth_store_->LoadUsers(rows).IsOk())return false;

        {
            std::lock_guard lock(store_mutex_);
            users_.clear();
            by_username_.clear();
            by_email_.clear();
            by_phone_.clear();

            for(auto& row:rows){
                UserRecord u{};
                u.id=std::move(row.id);
                u.username=std::move(row.username);
                u.email=std::move(row.email);
                u.display_name=std::move(row.display_name);
                u.avatar_url=std::move(row.avatar_url);
                u.phone=std::move(row.phone);
                u.salt_hex=std::move(row.salt_hex);
                u.verifier_hex=std::move(row.verifier_hex);
                u.email_verified=row.email_verified;
                u.phone_verified=row.phone_verified;
                u.mfa_enabled=row.mfa_enabled;
                u.mfa_recovery_hash=std::move(row.mfa_recovery_hash);
                u.mfa_totp_secret_hex=std::move(row.mfa_totp_secret_hex);
                u.email_verify_hash=std::move(row.email_verify_hash);
                u.email_verify_expires=row.email_verify_expires;
                u.reset_token_hash=std::move(row.reset_token_hash);
                u.reset_token_expires=row.reset_token_expires;

                users_[u.id]=u;
                by_username_[normalize(u.username)]=u.id;
                by_email_[normalize(u.email)]=u.id;
                if(u.phone_verified&&!u.phone.empty())by_phone_[u.phone]=u.id;
            }
        }

        std::vector<AuthSecurityEventRecord> events;
        if(!auth_store_->LoadSecurityEvents(events).IsOk())return false;
        {
            std::lock_guard lock(audit_mutex_);
            audits_.clear();
            audits_.reserve(events.size());
            for(auto& event:events){
                AuditRecord record{};
                record.user_id=std::move(event.user_id);
                record.security_event.event_id=std::move(event.event_id);
                record.security_event.type=std::move(event.type);
                record.security_event.detail=std::move(event.detail);
                record.security_event.created_at_epoch_seconds=event.created_at_epoch_seconds;
                audits_.push_back(std::move(record));
            }
        }
        return true;
    }

    bool SaveUnlocked(){
        if(!auth_store_)return false;
        std::vector<AuthUserRecord> rows;
        rows.reserve(users_.size());
        for(const auto&[_,u]:users_){
            AuthUserRecord row{};
            row.id=u.id;
            row.username=u.username;
            row.email=u.email;
            row.display_name=u.display_name;
            row.avatar_url=u.avatar_url;
            row.phone=u.phone;
            row.salt_hex=u.salt_hex;
            row.verifier_hex=u.verifier_hex;
            row.email_verified=u.email_verified;
            row.phone_verified=u.phone_verified;
            row.mfa_enabled=u.mfa_enabled;
            row.mfa_recovery_hash=u.mfa_recovery_hash;
            row.mfa_totp_secret_hex=u.mfa_totp_secret_hex;
            row.email_verify_hash=u.email_verify_hash;
            row.email_verify_expires=u.email_verify_expires;
            row.reset_token_hash=u.reset_token_hash;
            row.reset_token_expires=u.reset_token_expires;
            rows.push_back(std::move(row));
        }
        return auth_store_->ReplaceUsers(rows).IsOk();
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
        record.security_event.event_id=make_id();
        record.security_event.type=type;
        record.security_event.detail=detail;
        record.security_event.created_at_epoch_seconds=now_epoch();

        {
            std::lock_guard lock(audit_mutex_);
            audits_.push_back(record);
            if(audits_.size()>512)audits_.erase(audits_.begin());
        }

        if(auth_store_){
            AuthSecurityEventRecord event{};
            event.event_id=record.security_event.event_id;
            event.user_id=record.user_id;
            event.type=record.security_event.type;
            event.detail=record.security_event.detail;
            event.created_at_epoch_seconds=record.security_event.created_at_epoch_seconds;
            (void)auth_store_->AppendSecurityEvent(event);
        }
    }

    static std::string LoginRateKey(const std::string& identifier,const std::string& remote){
        return "login|identifier|"+normalize(identifier)+"|"+remote;
    }

    static std::string LoginUserRateKey(const std::string& user_id,const std::string& remote){
        return "login|user|"+user_id+"|"+remote;
    }

    bool IsLocked(const std::string& key,std::int64_t now){
        std::lock_guard lock(rate_mutex_);
        auto it=failures_.find(key);
        if(it==failures_.end())return false;
        if(it->second.locked_until>now)return true;
        if(it->second.locked_until!=0)it->second={};
        return false;
    }

    void RecordLoginFailure(
        const std::string& identifier,const std::string& user_id,const std::string& remote){
        const auto key=user_id.empty()
            ? LoginRateKey(identifier,remote)
            : LoginUserRateKey(user_id,remote);
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
        AppendAudit(
            user_id,
            "login_failure",
            locked?"login rate limit reached for source":"invalid credentials");
    }

    void ClearLoginFailures(
        const std::string& identifier,const std::string& remote){
        std::lock_guard lock(rate_mutex_);
        failures_.erase(LoginRateKey(identifier,remote));
    }

    void ClearUserLoginFailures(
        const std::string& user_id,const std::string& remote){
        std::lock_guard lock(rate_mutex_);
        failures_.erase(LoginUserRateKey(user_id,remote));
    }

    bool TooManyPhoneCodeRequests(const std::string& phone,const std::string& remote){
        const auto key="phone-code|"+phone+"|"+remote;
        const auto now=now_epoch();
        std::lock_guard lock(rate_mutex_);
        auto&state=failures_[key];
        if(state.window_started==0||now-state.window_started>600){
            state.window_started=now;
            state.count=0;
            state.locked_until=0;
        }
        if(state.locked_until>now)return true;
        if(state.count>=3){
            state.locked_until=now+600;
            return true;
        }
        ++state.count;
        state.locked_until=now+60;
        return false;
    }

    bool TooManyPhoneSourceRequests(const std::string& remote){
        const auto key="phone-source|"+remote;
        const auto now=now_epoch();
        std::lock_guard lock(rate_mutex_);
        auto&state=failures_[key];
        if(state.window_started==0||now-state.window_started>600){
            state.window_started=now;
            state.count=0;
            state.locked_until=0;
        }
        if(state.locked_until>now)return true;
        if(state.count>=10){
            state.locked_until=now+600;
            return true;
        }
        ++state.count;
        return false;
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
            if(IsLocked(LoginRateKey(identifier_key,c.remote_address),now_epoch())){
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

            c.device_id=p.fields[1];
            c.device_name=p.fields[2];

            if(user_id.empty()){
                // Keep the login protocol shape identical for unknown identifiers.
                // The user existence check is deferred until LoginProof.
                c.pending=Pending{
                    Pending::Kind::Login,
                    p.fields[0],
                    "",
                    "",
                    "",
                    hex(random_bytes(16)),
                    hex(random_bytes(32)),
                    ""
                };
                send(Type::LoginChallenge,{
                    c.pending.salt_hex,c.pending.nonce_hex,"0"});
                return;
            }

            const auto&u=users_.at(user_id);
            if(IsLocked(LoginUserRateKey(u.id,c.remote_address),now_epoch())){
                bad("temporarily_locked","too many failed login attempts; try again later");
                return;
            }

            c.pending=Pending{
                Pending::Kind::Login,
                "",
                "",
                "",
                u.id,
                u.salt_hex,
                hex(random_bytes(32)),
                ""
            };
            send(Type::LoginChallenge,{
                c.pending.salt_hex,c.pending.nonce_hex,u.mfa_enabled?"1":"0"});
            return;
        }

        case Type::LoginProof: {
            if(c.pending.kind!=Pending::Kind::Login||p.fields.size()!=2){
                bad("invalid_state","login challenge missing");return;
            }
            const auto user_id=c.pending.user_id;
            const auto login_identifier=c.pending.username;
            std::lock_guard lock(store_mutex_);
            if(user_id.empty()){
                c.pending={};
                RecordLoginFailure(login_identifier,{},c.remote_address);
                bad("invalid_credentials","invalid username or password");
                return;
            }

            const auto it=users_.find(user_id);
            if(it==users_.end()){
                c.pending={};
                RecordLoginFailure(login_identifier,{},c.remote_address);
                bad("invalid_credentials","invalid username or password");
                return;
            }

            try{
                const auto verifier=from_hex(it->second.verifier_hex);
                const auto nonce=from_hex(c.pending.nonce_hex);
                const auto supplied=from_hex(p.fields[0]);
                const auto expected=hmac_sha256(verifier,nonce);

                bool proof_ok=constant_time_equal(expected,supplied);
                bool mfa_ok=true;
                if(it->second.mfa_enabled){
                    mfa_ok=false;
                    if(!p.fields[1].empty()&&!it->second.mfa_recovery_hash.empty()){
                        try{
                            mfa_ok=constant_time_equal(
                                from_hex(it->second.mfa_recovery_hash),
                                from_hex(sha256_text(p.fields[1])));
                        }catch(...){mfa_ok=false;}
                    }
                    if(!mfa_ok&&!it->second.mfa_totp_secret_hex.empty()){
                        try{
                            const auto secret=base32_encode(from_hex(it->second.mfa_totp_secret_hex));
                            mfa_ok=verify_totp(secret,p.fields[1],now_epoch(),1);
                        }catch(...){mfa_ok=false;}
                    }
                }

                if(!proof_ok||!mfa_ok){
                    c.pending={};
                    RecordLoginFailure(user_id,user_id,c.remote_address);
                    bad("invalid_credentials","invalid username or password");return;
                }
            }catch(...){
                c.pending={};
                RecordLoginFailure(user_id,user_id,c.remote_address);
                bad("invalid_credentials","invalid username or password");return;
            }

            ClearUserLoginFailures(user_id,c.remote_address);
            ClearLoginFailures(login_identifier,c.remote_address);

            const auto session_id=make_id();
            const auto token=make_token();
            const auto refresh_token=make_token();
            const auto now=now_epoch();
            const auto expires=now+24*60*60;
            const auto refresh_expires=now+30*24*60*60;

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
                    session_id,user_id,c.device_id,c.device_name,c.remote_address,
                    sha256_text(refresh_token),now,now,expires,refresh_expires};
            }

            c.pending={};
            c.token=token;

            AppendAudit(user_id,"login_success","login succeeded");
            if(new_network)AppendAudit(user_id,"new_network","login from an unseen network address");
            send(Type::LoginOk,{
                token,refresh_token,session_id,c.device_id,c.device_name,it->second.id,
                it->second.username,it->second.email,it->second.display_name,
                std::to_string(expires),std::to_string(refresh_expires)});
            return;
        }

        case Type::RefreshSession: {
            if(p.fields.size()!=2||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            const auto user_id=require_session();
            if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
            if(p.fields[1].empty()){
                bad("invalid_refresh_token","refresh token is required");return;
            }

            std::string new_access;
            std::string new_refresh;
            SessionRecord session;
            const auto now=now_epoch();
            {
                std::lock_guard sl(session_mutex_);
                const auto it=sessions_.find(c.token);
                if(it==sessions_.end()||it->second.refresh_expires<=now){
                    bad("invalid_refresh_token","refresh token expired or invalid");return;
                }
                bool valid=false;
                try{
                    valid=constant_time_equal(
                        from_hex(it->second.refresh_token_hash),
                        from_hex(sha256_text(p.fields[1])));
                }catch(...){valid=false;}
                if(!valid){
                    bad("invalid_refresh_token","refresh token expired or invalid");return;
                }

                new_access=make_token();
                new_refresh=make_token();
                session=it->second;
                session.refresh_token_hash=sha256_text(new_refresh);
                session.last_seen=now;
                session.expires=now+24*60*60;
                sessions_.erase(it);
                sessions_[new_access]=session;
            }

            std::lock_guard lock(store_mutex_);
            const auto it=users_.find(user_id);
            if(it==users_.end()){bad("invalid_session","account no longer exists");return;}
            c.token=new_access;
            AppendAudit(user_id,"session_refreshed","access and refresh tokens rotated");
            send(Type::RefreshSessionOk,{
                new_access,new_refresh,session.session_id,session.device_id,session.device_name,
                it->second.id,it->second.username,it->second.email,it->second.display_name,
                std::to_string(session.expires),std::to_string(session.refresh_expires)});
            return;
        }

        case Type::Logout:
            if(p.fields.size()!=1||c.token.empty()||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            {
                const auto user_id=require_session();
                {
                    std::lock_guard sl(session_mutex_);
                    sessions_.erase(c.token);
                }
                AppendAudit(user_id,"logout","current session signed out");
            }
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
            std::string refresh_token;
            std::int64_t refresh_expires=0;
            {
                std::lock_guard sl(session_mutex_);
                const auto sit=sessions_.find(c.token);
                if(sit==sessions_.end()){bad("invalid_session","session expired or invalid");return;}
                // Refresh tokens are never persisted in plaintext; this response is only for
                // the current connection, so the client keeps its token from the login/refresh response.
                // ValidateSession therefore does not re-issue a refresh token.
                send(Type::LoginOk,{
                    c.token,std::string(),sit->second.session_id,sit->second.device_id,sit->second.device_name,
                    it->second.id,it->second.username,it->second.email,it->second.display_name,
                    std::to_string(sit->second.expires),std::to_string(sit->second.refresh_expires)});
            }
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
                send(Type::ProfileOk,{it->second.id,it->second.username,it->second.email,it->second.display_name,it->second.avatar_url,it->second.phone});
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
                send(Type::UpdateProfileOk,{u.id,u.username,u.email,u.display_name,u.avatar_url,u.phone});
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
                by_email_.erase(email);                if(old.phone_verified&&!old.phone.empty())by_phone_.erase(old.phone);


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
            const auto expires=now_epoch()+30*60;
            it->second.email_verify_hash=sha256_text(token);
            it->second.email_verify_expires=expires;
            const auto delivery=SendEmailToken(
                it->second.email,"email_verification",token,std::chrono::seconds(30*60));
            if(!delivery.accepted){
                it->second.email_verify_hash.clear();
                it->second.email_verify_expires=0;
                bad("email_delivery_failed","unable to send email verification message");
                return;
            }
            if(!SaveUnlocked()){bad("storage_error","unable to persist verification challenge");return;}
            AppendAudit(user_id,"email_verification_requested","verification challenge issued");
            send(Type::EmailVerificationIssued,{delivery.debug_token,std::to_string(expires)});
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

        case Type::RequestPhoneVerification: {
            if(p.fields.size()!=2||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            const auto user_id=require_session();
            if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}
            const auto phone=normalize_phone(p.fields[1]);
            if(phone.empty()){bad("invalid_phone","invalid phone number");return;}

            const auto challenge_id=make_id();
            const auto code=make_otp_code();
            const auto expires=now_epoch()+5*60;

            {
                std::lock_guard phone_lock(phone_challenge_mutex_);
                std::lock_guard lock(store_mutex_);
                const auto user_it=users_.find(user_id);
                if(user_it==users_.end()){bad("invalid_session","account no longer exists");return;}

                const auto existing=by_phone_.find(phone);
                if(existing!=by_phone_.end()&&existing->second!=user_id){
                    bad("phone_exists","phone number is already bound to another account");return;
                }

                phone_challenges_[challenge_id]=PhoneChallenge{
                    PhoneChallenge::Kind::Verification,phone,user_id,
                    sha256_text(challenge_id+"|"+code),expires,5};
            }

            const auto delivery=SendSmsOtp(phone,"phone_verification",code,std::chrono::seconds(5*60));
            if(!delivery.accepted){
                {
                    std::lock_guard phone_lock(phone_challenge_mutex_);
                    phone_challenges_.erase(challenge_id);
                }
                AppendAudit(user_id,"phone_verification_delivery_failed","SMS provider rejected verification code");
                bad("sms_delivery_failed","unable to send SMS verification code");
                return;
            }

            AppendAudit(user_id,"phone_verification_code_requested","SMS verification code accepted by provider");
            send(Type::PhoneVerificationIssued,{
                challenge_id,delivery.debug_code,std::to_string(expires)});
            return;
        }

        case Type::VerifyPhone: {
            std::lock_guard phone_lock(phone_challenge_mutex_);
            if(p.fields.size()!=3||p.fields[0]!=c.token){
                bad("invalid_session","invalid session");return;
            }
            const auto user_id=require_session();
            if(user_id.empty()){bad("invalid_session","session expired or invalid");return;}

            auto challenge_it=phone_challenges_.find(p.fields[1]);
            if(challenge_it==phone_challenges_.end()||
               challenge_it->second.kind!=PhoneChallenge::Kind::Verification||
               challenge_it->second.user_id!=user_id){
                bad("invalid_token","phone verification challenge is invalid or expired");return;
            }

            auto challenge=challenge_it->second;
            if(challenge.expires<now_epoch()){
                phone_challenges_.erase(challenge_it);
                bad("verification_expired","phone verification challenge expired");return;
            }

            bool code_ok=false;
            try{
                code_ok=constant_time_equal(
                    from_hex(challenge.code_hash),
                    from_hex(sha256_text(p.fields[1]+"|"+p.fields[2])));
            }catch(...){code_ok=false;}

            if(!code_ok){
                if(challenge.attempts_remaining>0)--challenge.attempts_remaining;
                if(challenge.attempts_remaining==0)phone_challenges_.erase(challenge_it);
                else challenge_it->second=challenge;
                bad("invalid_token",challenge.attempts_remaining==0
                    ?"too many invalid phone verification attempts"
                    :"invalid phone verification code");
                return;
            }

            std::lock_guard lock(store_mutex_);
            auto user_it=users_.find(user_id);
            if(user_it==users_.end()){phone_challenges_.erase(challenge_it);bad("invalid_session","account no longer exists");return;}
            const auto existing=by_phone_.find(challenge.phone);
            if(existing!=by_phone_.end()&&existing->second!=user_id){
                phone_challenges_.erase(challenge_it);
                bad("phone_exists","phone number is already bound to another account");return;
            }

            const auto old_phone=user_it->second.phone;
            const auto old_verified=user_it->second.phone_verified;
            if(old_verified&&!old_phone.empty()&&old_phone!=challenge.phone)by_phone_.erase(old_phone);

            user_it->second.phone=challenge.phone;
            user_it->second.phone_verified=true;
            if(!SaveUnlocked()){
                user_it->second.phone=old_phone;
                user_it->second.phone_verified=old_verified;
                if(old_verified&&!old_phone.empty())by_phone_[old_phone]=user_id;
                phone_challenges_.erase(challenge_it);
                bad("storage_error","unable to persist phone verification");return;
            }

            by_phone_[challenge.phone]=user_id;
            phone_challenges_.erase(challenge_it);
            AppendAudit(user_id,"phone_verified","phone number verified");
            send(Type::PhoneVerified,{challenge.phone});
            return;
        }

        case Type::RequestPhoneLoginCode: {
            if(p.fields.size()!=1){
                bad("invalid_phone","invalid phone request");return;
            }
            const auto phone=normalize_phone(p.fields[0]);
            if(phone.empty()){bad("invalid_phone","invalid phone number");return;}

            if(TooManyPhoneSourceRequests(c.remote_address)||
               TooManyPhoneCodeRequests(phone,c.remote_address)){
                bad("temporarily_locked","too many SMS code requests; try again later");return;
            }

            std::string user_id;
            {
                std::lock_guard lock(store_mutex_);
                const auto it=by_phone_.find(phone);
                if(it!=by_phone_.end()){
                    const auto u=users_.find(it->second);
                    if(u!=users_.end()&&u->second.phone_verified)user_id=u->second.id;
                }
            }

            const auto challenge_id=make_id();
            const auto code=make_otp_code();
            const auto expires=now_epoch()+5*60;
            {
                std::lock_guard phone_lock(phone_challenge_mutex_);
                phone_challenges_[challenge_id]=PhoneChallenge{
                    PhoneChallenge::Kind::Login,phone,user_id,
                    sha256_text(challenge_id+"|"+code),expires,5};
            }

            std::string debug_code;
            if(!user_id.empty()){
                const auto delivery=SendSmsOtp(
                    phone,"phone_login",code,std::chrono::seconds(5*60));
                if(delivery.accepted){
                    debug_code=delivery.debug_code;
                    AppendAudit(user_id,"phone_login_code_requested","SMS login code accepted by provider");
                }else{
                    {
                        std::lock_guard phone_lock(phone_challenge_mutex_);
                        phone_challenges_.erase(challenge_id);
                    }
                    AppendAudit(user_id,"phone_login_delivery_failed","SMS provider rejected login code");
                }
            }

            // Keep the request response shape identical in all cases. An unknown
            // phone number and a provider failure do not expose account existence.
            send(Type::PhoneLoginCodeIssued,{challenge_id,debug_code,std::to_string(expires)});
            return;
        }

        case Type::PhoneLogin: {
            std::lock_guard phone_lock(phone_challenge_mutex_);
            if(p.fields.size()!=4&&p.fields.size()!=5){
                bad("invalid_phone_login","challenge, code, device id and device name are required");return;
            }
            const auto challenge_id=p.fields[0];
            auto challenge_it=phone_challenges_.find(challenge_id);
            if(challenge_it==phone_challenges_.end()||
               challenge_it->second.kind!=PhoneChallenge::Kind::Login){
                bad("invalid_token","phone login challenge is invalid or expired");return;
            }

            auto challenge=challenge_it->second;
            if(challenge.expires<now_epoch()){
                phone_challenges_.erase(challenge_it);
                bad("verification_expired","phone login challenge expired");return;
            }

            bool code_ok=false;
            try{
                code_ok=constant_time_equal(
                    from_hex(challenge.code_hash),
                    from_hex(sha256_text(challenge_id+"|"+p.fields[1])));
            }catch(...){code_ok=false;}

            const auto user_id=challenge.user_id;
            if(!code_ok){
                if(challenge.attempts_remaining>0)--challenge.attempts_remaining;
                if(challenge.attempts_remaining==0)phone_challenges_.erase(challenge_it);
                else challenge_it->second=challenge;
                if(!user_id.empty())AppendAudit(user_id,"phone_login_failure",
                    challenge.attempts_remaining==0?"SMS code attempts exhausted":"invalid SMS code");
                bad("invalid_credentials","invalid phone login code");
                return;
            }

            std::lock_guard lock(store_mutex_);
            if(user_id.empty()){
                phone_challenges_.erase(challenge_it);
                bad("invalid_credentials","invalid phone login code");return;
            }
            const auto it=users_.find(user_id);
            if(it==users_.end()||!it->second.phone_verified||it->second.phone!=challenge.phone){
                phone_challenges_.erase(challenge_it);
                bad("invalid_credentials","invalid phone login code");return;
            }

            const std::string mfa_code=p.fields.size()==5?p.fields[4]:"";
            if(it->second.mfa_enabled){
                bool mfa_ok=false;
                if(!mfa_code.empty()&&!it->second.mfa_recovery_hash.empty()){
                    try{
                        mfa_ok=constant_time_equal(
                            from_hex(it->second.mfa_recovery_hash),
                            from_hex(sha256_text(mfa_code)));
                    }catch(...){mfa_ok=false;}
                }
                if(!mfa_ok&&!it->second.mfa_totp_secret_hex.empty()){
                    try{
                        const auto secret=base32_encode(from_hex(it->second.mfa_totp_secret_hex));
                        mfa_ok=verify_totp(secret,mfa_code,now_epoch(),1);
                    }catch(...){mfa_ok=false;}
                }
                if(!mfa_ok){
                    phone_challenges_.erase(challenge_it);
                    AppendAudit(user_id,"phone_login_failure","invalid MFA code");
                    bad("invalid_credentials","invalid MFA code");return;
                }
            }

            phone_challenges_.erase(challenge_it);
            c.device_id=p.fields[2];
            c.device_name=p.fields[3];
            const auto session_id=make_id();
            const auto token=make_token();
            const auto refresh_token=make_token();
            const auto now=now_epoch();
            const auto expires=now+24*60*60;
            const auto refresh_expires=now+30*24*60*60;
            bool new_network=true;
            {
                std::lock_guard sl(session_mutex_);
                for(const auto&[_,session]:sessions_){
                    if(session.user_id==user_id&&session.remote_address==c.remote_address){
                        new_network=false;break;
                    }
                }
                sessions_[token]=SessionRecord{
                    session_id,user_id,c.device_id,c.device_name,c.remote_address,
                    sha256_text(refresh_token),now,now,expires,refresh_expires};
            }
            c.token=token;
            AppendAudit(user_id,"phone_login_success","SMS login succeeded");
            if(new_network)AppendAudit(user_id,"new_network","login from an unseen network address");
            send(Type::LoginOk,{
                token,refresh_token,session_id,c.device_id,c.device_name,it->second.id,
                it->second.username,it->second.email,it->second.display_name,
                std::to_string(expires),std::to_string(refresh_expires)});
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
            const auto expires=now_epoch()+30*60;
            u.reset_token_hash=sha256_text(token);
            u.reset_token_expires=expires;
            const auto delivery=SendEmailToken(
                u.email,"password_reset",token,std::chrono::seconds(30*60));
            if(!delivery.accepted){
                u.reset_token_hash.clear();
                u.reset_token_expires=0;
                bad("email_delivery_failed","unable to send password reset message");
                return;
            }

            if(!SaveUnlocked()){bad("storage_error","unable to persist reset challenge");return;}
            AppendAudit(user_id,"password_reset_requested","password reset challenge issued");
            send(Type::PasswordResetIssued,{delivery.debug_token,salt_hex,std::to_string(expires)});
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
                    auto ui=failures_.find(LoginUserRateKey(it->second.id,c.remote_address));
                    if(ui!=failures_.end())failed=ui->second.count;
                }

                std::uint32_t active=0;
                {
                    std::lock_guard sl(session_mutex_);
                    for(const auto&[_,s]:sessions_)if(s.user_id==user_id&&s.expires>now_epoch())++active;
                }
                send(Type::SecuritySummaryOk,{
                    it->second.email_verified?"1":"0",
                    it->second.phone_verified?"1":"0",
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
                const auto totp_secret_hex=hex(random_bytes(20));
                const auto totp_secret_base32=base32_encode(from_hex(totp_secret_hex));
                it->second.mfa_recovery_hash=sha256_text(recovery_code);
                it->second.mfa_totp_secret_hex=totp_secret_hex;
                it->second.mfa_enabled=true;
                if(!SaveUnlocked()){bad("storage_error","unable to persist MFA setting");return;}
                const auto account=it->second.email.empty()?it->second.username:it->second.email;
                const auto otpauth=make_otpauth_uri(totp_secret_base32,account,"LumaLive");
                AppendAudit(user_id,"mfa_enabled","TOTP MFA enabled with a recovery code");
                send(Type::MfaEnabled,{recovery_code,totp_secret_base32,otpauth});
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
                bool credential_ok=false;
                if(!it->second.mfa_recovery_hash.empty()){
                    credential_ok=sha256_text(p.fields[1])==it->second.mfa_recovery_hash;
                }
                if(!credential_ok&&!it->second.mfa_totp_secret_hex.empty()){
                    try{
                        const auto secret=base32_encode(from_hex(it->second.mfa_totp_secret_hex));
                        credential_ok=verify_totp(secret,p.fields[1],now_epoch(),1);
                    }catch(...){credential_ok=false;}
                }
                if(!credential_ok){
                    bad("invalid_mfa_code","invalid MFA recovery code or TOTP code");return;
                }
                it->second.mfa_enabled=false;
                it->second.mfa_recovery_hash.clear();
                it->second.mfa_totp_secret_hex.clear();
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
                    fields.push_back(e->security_event.event_id);
                    fields.push_back(e->security_event.type);
                    fields.push_back(e->security_event.detail);
                    fields.push_back(std::to_string(e->security_event.created_at_epoch_seconds));
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
        case Type::PhoneVerificationIssued:
        case Type::PhoneVerified:
        case Type::PhoneLoginCodeIssued:
        case Type::RefreshSessionOk:
        case Type::Error:
        case Type::Pong:
            bad("unexpected_packet","unexpected authentication packet");return;

        default:
            bad("unsupported","unsupported auth operation");return;
        }
    }

    std::atomic<bool> running_{false};
    std::atomic<Socket> listen_socket_{kInvalidSocket};
    std::uint16_t port_{0};
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    mutable std::mutex lifecycle_mutex_,clients_mutex_,store_mutex_,session_mutex_,rate_mutex_,audit_mutex_;
    std::unique_ptr<IAuthStore> auth_store_;
    std::unique_ptr<ISmsProvider> sms_provider_;
    std::unique_ptr<IEmailProvider> email_provider_;
    std::unordered_map<Socket,ClientState> clients_;
    std::unordered_map<std::string,SessionRecord> sessions_;
    std::unordered_map<std::string,UserRecord> users_;
    std::unordered_map<std::string,std::string> by_username_,by_email_,by_phone_;
    std::unordered_map<std::string,FailureState> failures_;
    std::unordered_map<std::string,PhoneChallenge> phone_challenges_;
    mutable std::mutex phone_challenge_mutex_;
    std::vector<AuditRecord> audits_;
};

std::unique_ptr<IAuthService>CreateAuthService(){
    return std::make_unique<AuthServiceImpl>();
}

std::unique_ptr<IAuthService>CreateAuthService(std::unique_ptr<IAuthStore> store){
    return std::make_unique<AuthServiceImpl>(std::move(store));
}

}
