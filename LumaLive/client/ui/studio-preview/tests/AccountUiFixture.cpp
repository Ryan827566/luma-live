// Test-only in-memory account server and delivery mailbox. Never contacts a provider.
#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "IEmailProvider.hpp"
#include "ISmsProvider.hpp"
#include <cstdlib>
#include <atomic>
#include <iostream>
#include <mutex>
#include <unordered_map>
using namespace luma::server::auth;
namespace shared=luma::shared;
namespace {
struct Mailbox {
    std::mutex mutex;
    std::unordered_map<std::string,std::string> messages;
    bool rejectEmail{},rejectSms{};
    std::atomic<bool> rejectStorage{false};
    bool Deliver(std::string_view purpose,std::string_view code,bool email){
        std::lock_guard lock(mutex);
        if(email?rejectEmail:rejectSms)return false;
        messages[std::string(purpose)]=code;return true;
    }
    std::string Command(const std::string& line){
        std::lock_guard lock(mutex);
        if(line=="reject-email"){rejectEmail=true;return "OK";}
        if(line=="reject-sms"){rejectSms=true;return "OK";}
        if(line=="accept-delivery"){rejectEmail=rejectSms=false;return "OK";}
        if(line=="reject-storage"){rejectStorage=true;return "OK";}
        if(line=="accept-storage"){rejectStorage=false;return "OK";}
        auto found=messages.find(line);if(found==messages.end())return "MISSING";
        auto value=std::move(found->second);messages.erase(found);return value;
    }
};
class Store final:public IAuthStore {
    std::unique_ptr<IAuthStore> delegate{CreateInMemoryAuthStore()};std::shared_ptr<Mailbox> mailbox;
public:
    explicit Store(std::shared_ptr<Mailbox> value):mailbox(std::move(value)){}
    shared::contracts::Result Open()override{return delegate->Open();}
    void Close()noexcept override{delegate->Close();}
    shared::contracts::Result LoadUsers(std::vector<AuthUserRecord>& rows)override{return delegate->LoadUsers(rows);}
    shared::contracts::Result UpsertUser(const AuthUserRecord& row)override{
        if(mailbox->rejectStorage)return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal,"fixture storage rejection");
        return delegate->UpsertUser(row);
    }
    shared::contracts::Result DeleteUser(std::string_view id)override{return delegate->DeleteUser(id);}
    shared::contracts::Result LoadSecurityEvents(std::vector<AuthSecurityEventRecord>& rows)override{return delegate->LoadSecurityEvents(rows);}
    shared::contracts::Result AppendSecurityEvent(const AuthSecurityEventRecord& row)override{return delegate->AppendSecurityEvent(row);}
};
class Email final:public IEmailProvider {
    std::shared_ptr<Mailbox> mailbox;
public:
    explicit Email(std::shared_ptr<Mailbox> value):mailbox(std::move(value)){}
    bool IsConfigured()const override{return true;}
    EmailSendResult SendToken(std::string_view,std::string_view purpose,std::string_view token,std::chrono::seconds)override{
        const bool ok=mailbox->Deliver(purpose,token,true);
        return {ok,ok?"fixture-email":"",{},ok?"accepted":"fixture rejected delivery"};
    }
};
class Sms final:public ISmsProvider {
    std::shared_ptr<Mailbox> mailbox;
public:
    explicit Sms(std::shared_ptr<Mailbox> value):mailbox(std::move(value)){}
    bool IsConfigured()const override{return true;}
    SmsSendResult SendOtp(std::string_view,std::string_view purpose,std::string_view code,std::chrono::seconds)override{
        const bool ok=mailbox->Deliver(purpose,code,false);
        return {ok,ok?"fixture-sms":"",{},ok?"accepted":"fixture rejected delivery"};
    }
};
}
int main(int argc,char** argv){
    _putenv_s("LUMALIVE_AUTH_ENV","test");
    _putenv_s("LUMALIVE_AUTH_TLS_CERT_FILE","");
    _putenv_s("LUMALIVE_AUTH_TLS_KEY_FILE","");
    auto mailbox=std::make_shared<Mailbox>();
    auto server=CreateAuthService(std::make_unique<Store>(mailbox),std::make_unique<Email>(mailbox),std::make_unique<Sms>(mailbox));
    const auto port=static_cast<unsigned short>(argc>1?std::stoi(argv[1]):19241);
    if(!server->StartOnPort(port).IsOk())return 1;
    std::cout<<server->Port()<<std::endl;
    // Only this test executable exposes synthetic delivery through its private stdin/stdout pipe.
    for(std::string line;std::getline(std::cin,line)&&!line.empty();)std::cout<<mailbox->Command(line)<<std::endl;
    server->Stop();return 0;
}
