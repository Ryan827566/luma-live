#include "IAccountService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include "contracts/auth/AuthWire.hpp"
#include <atomic>
#include <chrono>
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
using luma::contracts::auth::crypto::from_hex;
using luma::contracts::auth::crypto::hex;
using luma::contracts::auth::crypto::hmac_sha256;
using luma::contracts::auth::crypto::pbkdf2_hmac_sha256;
using luma::contracts::auth::crypto::sha256;
using luma::contracts::auth::wire::Packet;
using luma::contracts::auth::wire::Type;

namespace {
bool SetSocketTimeouts(Socket socket){
#ifdef _WIN32
    const DWORD timeout_ms=5000;
    return ::setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),sizeof(timeout_ms))==0&&
        ::setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),sizeof(timeout_ms))==0;
#else
    const timeval timeout{5,0};
    return ::setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0&&
        ::setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))==0;
#endif
}
}


class AccountService final:public IAccountService{
public:
    ~AccountService()override{Stop();}

    OperationResult Start()override{return Connect(host_,port_);}

    OperationResult Connect(std::string host,std::uint16_t port)override{
        Stop();
        if(host.empty()||!port||!init_sockets())return{false,"invalid account server address"};
        host_=std::move(host);port_=port;
        Socket s=::socket(AF_INET,SOCK_STREAM,0);
        if(s==kInvalidSocket)return{false,"socket creation failed"};
        if(!SetSocketTimeouts(s)){close_socket(s);return{false,"unable to configure account connection timeout"};}
        sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port_);
#ifdef _WIN32
        if(::InetPtonA(AF_INET,host_.c_str(),&a.sin_addr)!=1){close_socket(s);return{false,"host must be an IPv4 address"};}
#else
        if(::inet_pton(AF_INET,host_.c_str(),&a.sin_addr)!=1){close_socket(s);return{false,"host must be an IPv4 address"};}
#endif
        if(::connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0){close_socket(s);return{false,"unable to connect to auth server"};}
        socket_=s;running_=true;return{true,"connected"};
    }

    OperationResult Stop()override{
        std::lock_guard lock(mutex_);
        if(socket_!=kInvalidSocket){close_socket(socket_);socket_=kInvalidSocket;}
        running_=false;
        ClearSessionLocked();
        return{true,"stopped"};
    }

    bool IsRunning() const override{return running_.load();}

    OperationResult Register(std::string u,std::string e,std::string d,std::string p)override{
        if(!valid_register(u,e,d,p))return{false,"invalid registration data"};
        std::lock_guard lock(mutex_);
        if(!running_)return{false,"account service is not connected"};
        if(!Send({Type::RegisterBegin,{u,e,d}}))return{false,"send failed"};
        Packet q;
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        std::uint32_t iterations=0;
        if(!ValidPasswordChallenge(q,Type::RegisterChallenge,iterations))return InvalidChallengeLocked();
        const auto verifier=pbkdf2_hmac_sha256(p,from_hex(q.fields[0]),iterations);
        if(!Send({Type::RegisterFinish,{hex(verifier)} }))return{false,"send failed"};
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::RegisterOk||q.fields.size()!=4)return{false,"invalid registration response"};
        return{true,"registered user_id="+q.fields[0]};
    }

    OperationResult Login(
        std::string id,std::string p,std::string device_id,std::string device_name,std::string mfa_code)override{
        if(id.empty()||id.size()>254||p.size()<8||p.size()>128)return{false,"invalid login data"};
        std::lock_guard lock(mutex_);
        if(!running_)return{false,"account service is not connected"};
        if(device_id.empty())device_id="default-device";
        if(device_name.empty())device_name="LumaLive Client";
        if(!valid_text(device_id,128)||!valid_text(device_name,128))return{false,"invalid device information"};

        if(!Send({Type::LoginBegin,{id,device_id,device_name}}))return{false,"send failed"};
        Packet q;
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        std::uint32_t iterations=0;
        if(!ValidPasswordChallenge(q,Type::LoginChallenge,iterations))return InvalidChallengeLocked();
        const auto verifier=pbkdf2_hmac_sha256(p,from_hex(q.fields[0]),iterations);
        const auto proof=hmac_sha256(verifier,from_hex(q.fields[1]));
        const bool mfa_required=q.fields[2]=="1";
        (void)mfa_required;

        if(!Send({Type::LoginProof,{hex(proof),mfa_code}}))return{false,"send failed"};
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::LoginOk||q.fields.size()!=11)return{false,"invalid login response"};

        ClearSessionLocked();
        session_.token=q.fields[0];
        session_.refresh_token=q.fields[1];
        session_.session_id=q.fields[2];
        session_.device_id=q.fields[3];
        session_.device_name=q.fields[4];
        session_.user.user_id=q.fields[5];
        session_.user.username=q.fields[6];
        session_.user.email=q.fields[7];
        session_.user.display_name=q.fields[8];
        session_.user.avatar_url.clear();
        session_.expires_at_epoch_seconds=std::stoll(q.fields[9]);
        session_.refresh_expires_at_epoch_seconds=std::stoll(q.fields[10]);
        return{true,"login successful"};
    }

    OperationResult Logout()override{
        std::lock_guard lock(mutex_);
        if(!running_)return{false,"account service is not connected"};
        if(session_.token.empty())return{true,"already logged out"};
        if(!Send({Type::Logout,{session_.token}}))return{false,"send failed"};
        Packet q;
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::LogoutOk)return{false,"invalid logout response"};
        ClearSessionLocked();
        return{true,"logout successful"};
    }

    OperationResult ValidateSession()override{
        std::lock_guard lock(mutex_);
        if(!running_||session_.token.empty())return{false,"not authenticated"};
        if(!Send({Type::ValidateSession,{session_.token}}))return{false,"send failed"};
        Packet q;
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::LoginOk||q.fields.size()!=11)return{false,"invalid session response"};
        session_.token=q.fields[0];
        if(!q.fields[1].empty())session_.refresh_token=q.fields[1];
        session_.session_id=q.fields[2];
        session_.device_id=q.fields[3];
        session_.device_name=q.fields[4];
        session_.user.user_id=q.fields[5];
        session_.user.username=q.fields[6];
        session_.user.email=q.fields[7];
        session_.user.display_name=q.fields[8];
        session_.expires_at_epoch_seconds=std::stoll(q.fields[9]);
        session_.refresh_expires_at_epoch_seconds=std::stoll(q.fields[10]);
        return{true,"session valid"};
    }

    OperationResult RefreshSession()override{
        std::lock_guard lock(mutex_);
        if(!running_||session_.token.empty()||session_.refresh_token.empty())
            return{false,"refresh token is not available"};
        if(!Send({Type::RefreshSession,{session_.token,session_.refresh_token}}))
            return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::RefreshSessionOk||q.fields.size()!=11)
            return{false,"invalid refresh response"};
        session_.token=q.fields[0];
        session_.refresh_token=q.fields[1];
        session_.session_id=q.fields[2];
        session_.device_id=q.fields[3];
        session_.device_name=q.fields[4];
        session_.user.user_id=q.fields[5];
        session_.user.username=q.fields[6];
        session_.user.email=q.fields[7];
        session_.user.display_name=q.fields[8];
        session_.expires_at_epoch_seconds=std::stoll(q.fields[9]);
        session_.refresh_expires_at_epoch_seconds=std::stoll(q.fields[10]);
        return{true,"session refreshed"};
    }

    OperationResult GetProfile()override{
        std::lock_guard lock(mutex_);
        if(!running_||session_.token.empty())return{false,"not authenticated"};
        if(!Send({Type::GetProfile,{session_.token}}))return{false,"send failed"};
        Packet q;
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::ProfileOk||q.fields.size()!=6)return{false,"invalid profile response"};
        session_.user.user_id=q.fields[0];
        session_.user.username=q.fields[1];
        session_.user.email=q.fields[2];
        session_.user.display_name=q.fields[3];
        session_.user.avatar_url=q.fields[4];
        session_.user.phone=q.fields[5];
        return{true,"profile loaded"};
    }

    OperationResult UpdateProfile(std::string username,std::string email,std::string display_name,std::string avatar_url)override{
        if(!valid_profile(username,email,display_name,avatar_url))return{false,"invalid profile data"};
        std::lock_guard lock(mutex_);
        if(!running_||session_.token.empty())return{false,"not authenticated"};
        if(!Send({Type::UpdateProfile,{session_.token,username,email,display_name,avatar_url}}))return{false,"send failed"};
        Packet q;
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::UpdateProfileOk||q.fields.size()!=6)return{false,"invalid profile update response"};
        session_.user.user_id=q.fields[0];
        session_.user.username=q.fields[1];
        session_.user.email=q.fields[2];
        session_.user.display_name=q.fields[3];
        session_.user.avatar_url=q.fields[4];
        session_.user.phone=q.fields[5];
        return{true,"profile updated"};
    }

    OperationResult DeleteAccount()override{
        std::lock_guard lock(mutex_);
        if(!running_||session_.token.empty())return{false,"not authenticated"};
        if(!Send({Type::DeleteAccount,{session_.token}}))return{false,"send failed"};
        Packet q;
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::DeleteAccountOk)return{false,"invalid account deletion response"};
        session_={};security_={};sessions_.clear();events_.clear();
        pending_phone_verification_={};
        pending_phone_login_={};
        pending_phone_login_number_.clear();
        return{true,"account deleted"};
    }

    OperationResult RequestEmailVerification()override{
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::RequestEmailVerification,{session_.token}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::EmailVerificationIssued||q.fields.size()!=2)return{false,"invalid email verification response"};
        return{true,"email verification token="+q.fields[0]+" expires="+q.fields[1]};
    }

    OperationResult VerifyEmail(std::string verification_token)override{
        if(verification_token.empty())return{false,"verification token is empty"};
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::VerifyEmail,{session_.token,verification_token}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::EmailVerified)return{false,"invalid email verification response"};
        return{true,"email verified"};
    }

    OperationResult RequestPhoneVerification(std::string phone)override{
        if(phone.empty()||phone.size()>32)return{false,"invalid phone number"};
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::RequestPhoneVerification,{session_.token,phone}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::PhoneVerificationIssued||q.fields.size()!=3)
            return{false,"invalid phone verification response"};
        pending_phone_verification_=PendingPhoneChallenge{q.fields[0],std::stoll(q.fields[2])};
        return{true,"SMS verification code="+q.fields[1]+" challenge="+q.fields[0]+" expires="+q.fields[2]};
    }

    OperationResult VerifyPhone(std::string challenge_id,std::string code)override{
        if(challenge_id.empty()||code.empty())return{false,"invalid phone verification challenge"};
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::VerifyPhone,{session_.token,challenge_id,code}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::PhoneVerified||q.fields.size()!=1)
            return{false,"invalid phone verification response"};
        pending_phone_verification_={};
        session_.user.phone=q.fields[0];
        security_.phone_verified=true;
        return{true,"phone verified"};
    }

    OperationResult RequestPhoneLoginCode(std::string phone)override{
        if(phone.empty()||phone.size()>32)return{false,"invalid phone number"};
        std::lock_guard lock(mutex_);
        if(!running_)return{false,"account service is not connected"};
        if(!Send({Type::RequestPhoneLoginCode,{phone}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::PhoneLoginCodeIssued||q.fields.size()!=3)
            return{false,"invalid SMS login response"};
        pending_phone_login_=PendingPhoneChallenge{q.fields[0],std::stoll(q.fields[2])};
        pending_phone_login_number_=phone;
        return{true,"SMS login code="+q.fields[1]+" challenge="+q.fields[0]+" expires="+q.fields[2]};
    }

    OperationResult LoginWithPhoneCode(
        std::string challenge_id,
        std::string code,
        std::string device_id,
        std::string device_name,
        std::string mfa_code)override{
        if(challenge_id.empty()||code.empty())return{false,"invalid SMS login challenge"};
        std::lock_guard lock(mutex_);
        if(!running_)return{false,"account service is not connected"};
        if(device_id.empty())device_id="default-device";
        if(device_name.empty())device_name="LumaLive Client";
        if(!valid_text(device_id,128)||!valid_text(device_name,128))
            return{false,"invalid device information"};

        std::vector<std::string> fields{challenge_id,code,device_id,device_name};
        if(!mfa_code.empty())fields.push_back(mfa_code);
        if(!Send({Type::PhoneLogin,std::move(fields)}))return{false,"send failed"};

        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::LoginOk||q.fields.size()!=11)
            return{false,"invalid phone login response"};

        const auto phone=pending_phone_login_number_;
        ClearSessionLocked();
        session_.token=q.fields[0];
        session_.refresh_token=q.fields[1];
        session_.session_id=q.fields[2];
        session_.device_id=q.fields[3];
        session_.device_name=q.fields[4];
        session_.user.user_id=q.fields[5];
        session_.user.username=q.fields[6];
        session_.user.email=q.fields[7];
        session_.user.display_name=q.fields[8];
        session_.user.avatar_url.clear();
        session_.user.phone=phone;
        session_.expires_at_epoch_seconds=std::stoll(q.fields[9]);
        session_.refresh_expires_at_epoch_seconds=std::stoll(q.fields[10]);
        pending_phone_login_={};
        return{true,"phone SMS login successful"};
    }

    OperationResult ChangePassword(std::string current_password,std::string new_password)override{
        if(current_password.size()<8||current_password.size()>128||new_password.size()<8||new_password.size()>128)
            return{false,"invalid password data"};
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::ChangePasswordBegin,{session_.token}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        std::uint32_t current_iterations=0;
        if(!ValidPasswordChallenge(q,Type::ChangePasswordChallenge,current_iterations))return InvalidChallengeLocked();
        const auto current_verifier=pbkdf2_hmac_sha256(current_password,from_hex(q.fields[0]),current_iterations);
        const auto current_proof=hmac_sha256(current_verifier,from_hex(q.fields[1]));
        const auto new_verifier=pbkdf2_hmac_sha256(
            new_password,from_hex(q.fields[2]),luma::contracts::auth::crypto::kPasswordPbkdf2Iterations);
        if(!Send({Type::ChangePasswordFinish,{session_.token,hex(current_proof),hex(new_verifier)}}))return{false,"send failed"};
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::PasswordChanged)return{false,"invalid password change response"};
        session_={};security_={};sessions_.clear();events_.clear();
        pending_phone_verification_={};
        pending_phone_login_={};
        pending_phone_login_number_.clear();
        return{true,"password changed; all sessions signed out"};
    }

    OperationResult RequestPasswordReset(std::string identifier)override{
        if(identifier.empty()||identifier.size()>254)return{false,"invalid reset identifier"};
        std::lock_guard lock(mutex_);
        if(!running_)return{false,"account service is not connected"};
        if(!Send({Type::RequestPasswordReset,{identifier}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::PasswordResetIssued||q.fields.size()!=4)return{false,"invalid password reset response"};
        if(q.fields[0].empty()){
            return{true,"if the account exists, a reset message will be available through the configured delivery channel"};
        }
        return{true,"password reset token="+q.fields[0]+" expires="+q.fields[2]};
    }

    OperationResult ResetPassword(std::string reset_token,std::string new_password)override{
        if(reset_token.empty()||new_password.size()<8||new_password.size()>128)return{false,"invalid password reset data"};
        std::lock_guard lock(mutex_);
        if(!running_)return{false,"account service is not connected"};

        // The token may arrive by email or be entered after restarting the client.
        // Its validity belongs to the server; a fresh client salt needs no local challenge cache.
        const auto salt=luma::contracts::auth::crypto::random_bytes(16);
        const auto new_verifier=pbkdf2_hmac_sha256(
            new_password,salt,luma::contracts::auth::crypto::kPasswordPbkdf2Iterations);
        if(!Send({Type::ResetPassword,{reset_token,hex(salt),hex(new_verifier)}}))return{false,"send failed"};

        Packet q;
        if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::PasswordResetOk)return{false,"invalid password reset response"};

        session_={};
        security_={};
        sessions_.clear();
        events_.clear();
        pending_phone_verification_={};
        pending_phone_login_={};
        pending_phone_login_number_.clear();
        return{true,"password reset successful; all sessions signed out"};
    }

    OperationResult GetSecuritySummary()override{
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::GetSecuritySummary,{session_.token}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::SecuritySummaryOk||q.fields.size()!=5)return{false,"invalid security summary"};
        security_.email_verified=q.fields[0]=="1";
        security_.phone_verified=q.fields[1]=="1";
        security_.mfa_enabled=q.fields[2]=="1";
        security_.failed_login_attempts=static_cast<std::uint32_t>(std::stoul(q.fields[3]));
        security_.active_session_count=static_cast<std::uint32_t>(std::stoul(q.fields[4]));
        return{true,"security summary loaded"};
    }

    contracts::auth::SecuritySummary Security()const override{
        std::lock_guard lock(mutex_);return security_;
    }

    OperationResult EnableMfa()override{
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::EnableMfa,{session_.token}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::MfaEnabled||q.fields.size()!=3)return{false,"invalid MFA enable response"};
        security_.mfa_enabled=true;
        return{true,"MFA enabled; recovery codes="+q.fields[0]+"; TOTP secret="+q.fields[1]+"; otpauth="+q.fields[2]};
    }

    OperationResult DisableMfa(std::string recovery_code)override{
        if(recovery_code.empty())return{false,"recovery code is empty"};
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::DisableMfa,{session_.token,recovery_code}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::MfaDisabled)return{false,"invalid MFA disable response"};
        security_.mfa_enabled=false;
        return{true,"MFA disabled"};
    }

    OperationResult GetSessions()override{
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::GetSessions,{session_.token}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::SessionsOk||q.fields.empty())return{false,"invalid session list"};
        const auto count=static_cast<std::size_t>(std::stoul(q.fields[0]));
        if(q.fields.size()!=1+count*7)return{false,"invalid session list size"};
        sessions_.clear();
        for(std::size_t i=0;i<count;++i){
            const auto base=1+i*7;
            contracts::auth::DeviceSession s;
            s.session_id=q.fields[base];
            s.device_id=q.fields[base+1];
            s.device_name=q.fields[base+2];
            s.remote_address=q.fields[base+3];
            s.created_at_epoch_seconds=std::stoll(q.fields[base+4]);
            s.last_seen_epoch_seconds=std::stoll(q.fields[base+5]);
            s.current=q.fields[base+6]=="1";
            sessions_.push_back(std::move(s));
        }
        return{true,"sessions loaded"};
    }

    const std::vector<contracts::auth::DeviceSession>& Sessions()const override{return sessions_;}

    OperationResult RevokeSession(std::string session_id)override{
        if(session_id.empty())return{false,"session id is empty"};
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::RevokeSession,{session_.token,session_id}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::SessionRevoked)return{false,"invalid session revoke response"};
        if(session_id==session_.session_id)ClearSessionLocked();
        return{true,"session revoked"};
    }

    OperationResult RevokeOtherSessions()override{
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::RevokeOtherSessions,{session_.token}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::SessionsRevoked)return{false,"invalid session revoke response"};
        sessions_.clear();
        return{true,"other sessions revoked"};
    }

    OperationResult GetSecurityEvents()override{
        std::lock_guard lock(mutex_);
        if(!AuthenticatedLocked())return{false,"not authenticated"};
        if(!Send({Type::GetSecurityEvents,{session_.token}}))return{false,"send failed"};
        Packet q;if(!Recv(q))return{false,"receive failed"};
        if(q.type==Type::Error)return Error(q);
        if(q.type!=Type::SecurityEventsOk||q.fields.empty())return{false,"invalid security event list"};
        const auto count=static_cast<std::size_t>(std::stoul(q.fields[0]));
        if(q.fields.size()!=1+count*4)return{false,"invalid security event list size"};
        events_.clear();
        for(std::size_t i=0;i<count;++i){
            const auto base=1+i*4;
            contracts::auth::SecurityEvent e;
            e.event_id=q.fields[base];
            e.type=q.fields[base+1];
            e.detail=q.fields[base+2];
            e.created_at_epoch_seconds=std::stoll(q.fields[base+3]);
            events_.push_back(std::move(e));
        }
        return{true,"security events loaded"};
    }

    const std::vector<contracts::auth::SecurityEvent>& SecurityEvents()const override{return events_;}

    bool IsAuthenticated()const override{
        std::lock_guard lock(mutex_);
        return AuthenticatedLocked();
    }

    contracts::auth::AuthSession Session()const override{
        std::lock_guard lock(mutex_);return session_;
    }

    OperationResult Execute(std::string_view operation)override{
        if(operation.empty())return{false,"operation is empty"};
        return{true,std::string(operation)};
    }

private:
    // Validate the complete challenge before any password work, including fields
    // consumed only after the first derivation. Keep generic crypto primitives unchanged.
    static bool ValidPasswordChallenge(const Packet& packet,Type expected,std::uint32_t& iterations){
        if(packet.type!=expected||packet.fields.size()!=(expected==Type::RegisterChallenge?3u:4u))return false;
        const auto valid_hex=[](std::string_view text,std::size_t bytes){
            if(text.size()!=bytes*2)return false;
            for(char c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
            return true;
        };
        if(!valid_hex(packet.fields[0],16)||!valid_hex(packet.fields[1],32))return false;
        if(expected==Type::ChangePasswordChallenge&&!valid_hex(packet.fields[2],16))return false;
        if(expected==Type::LoginChallenge&&packet.fields[2]!="0"&&packet.fields[2]!="1")return false;
        const auto& text=packet.fields.back();
        if(text.empty()||text.size()>10)return false;
        constexpr auto maximum=luma::contracts::auth::crypto::kPasswordPbkdf2Iterations;
        iterations=0;
        for(char c:text){
            if(c<'0'||c>'9')return false;
            const auto digit=static_cast<std::uint32_t>(c-'0');
            if(iterations>(maximum-digit)/10)return false;
            iterations=iterations*10+digit;
        }
        return iterations!=0;
    }

    OperationResult InvalidChallengeLocked(){
        DisconnectLocked();
        return{false,"invalid authentication challenge","invalid_challenge"};
    }

    void ClearSessionLocked(){
        session_={};
        security_={};
        sessions_.clear();
        events_.clear();
        pending_phone_verification_={};
        pending_phone_login_={};
        pending_phone_login_number_.clear();
    }

    bool DisconnectLocked(){
        if(socket_!=kInvalidSocket){close_socket(socket_);socket_=kInvalidSocket;}
        running_=false;
        ClearSessionLocked();
        return false;
    }

    static bool valid_text(const std::string& value,std::size_t max){
        if(value.empty()||value.size()>max)return false;
        return luma::contracts::auth::wire::valid_field(value);
    }

    static bool valid_email(const std::string& value){
        if(value.size()<3||value.size()>254||!valid_text(value,254))return false;
        const auto at=value.find('@');
        return at!=std::string::npos&&at>0&&at+1<value.size()&&value.find('@',at+1)==std::string::npos;
    }

    static bool valid_username(const std::string& value){
        if(value.size()<3||value.size()>32||!valid_text(value,32))return false;
        for(char c:value)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
        return true;
    }

    static bool valid_profile(const std::string& u,const std::string& e,const std::string& d,const std::string& a){
        return valid_username(u)&&valid_email(e)&&valid_text(d,64)&&a.size()<=2048&&luma::contracts::auth::wire::valid_field(a);
    }

    static bool valid_register(const std::string&u,const std::string&e,const std::string&d,const std::string&p){
        return valid_username(u)&&valid_email(e)&&valid_text(d,64)&&p.size()>=8&&p.size()<=128;
    }

    bool AuthenticatedLocked()const{
        return running_.load()&&!session_.token.empty()&&session_.expires_at_epoch_seconds>
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    bool Send(const Packet&p){
        auto msg=luma::contracts::auth::wire::encode(p);
        std::size_t off=0;
        while(off<msg.size()){
            int n=::send(socket_,msg.data()+off,static_cast<int>(msg.size()-off),0);
            if(n<=0)return DisconnectLocked();
            off+=static_cast<std::size_t>(n);
        }
        return true;
    }

    bool Recv(Packet&out){
        std::string line;char c=0;
        while(true){
            const int n=::recv(socket_,&c,1,0);
            if(n<=0)return DisconnectLocked();
            if(c=='\n')break;
            line.push_back(c);
            if(line.size()>64*1024)return DisconnectLocked();
        }
        try{out=luma::contracts::auth::wire::decode_line(line);return true;}catch(...){return DisconnectLocked();}
    }

    OperationResult Error(const Packet&p){
        if(!p.fields.empty()){
            if(p.fields[0]=="invalid_session"||p.fields[0]=="invalid_refresh_token")ClearSessionLocked();
            else if(p.fields[0]=="access_token_expired")session_.expires_at_epoch_seconds=0;
        }
        return p.fields.size()>=2?OperationResult{false,p.fields[1],p.fields[0]}:OperationResult{false,"authentication server error"};
    }

    std::string host_{"127.0.0.1"};
    std::uint16_t port_{9100};
    std::atomic<bool>running_{false};
    Socket socket_{kInvalidSocket};
    mutable std::mutex mutex_;
    contracts::auth::AuthSession session_;
    contracts::auth::SecuritySummary security_;
    struct PendingPhoneChallenge{
        std::string challenge_id;
        std::int64_t expires_at{0};
    };
    std::vector<contracts::auth::DeviceSession> sessions_;
    std::vector<contracts::auth::SecurityEvent> events_;
    PendingPhoneChallenge pending_phone_verification_;
    PendingPhoneChallenge pending_phone_login_;
    std::string pending_phone_login_number_;
};

std::unique_ptr<IAccountService>CreateAccountService(){return std::make_unique<AccountService>();}

}
