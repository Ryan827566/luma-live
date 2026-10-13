// Test-only in-memory account server and delivery mailbox. Never contacts a provider.
#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "IEmailProvider.hpp"
#include "ISmsProvider.hpp"
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <unordered_map>
using namespace luma::server::auth;
namespace {
struct Mailbox {
    std::mutex mutex;
    std::unordered_map<std::string,std::string> messages;
    bool rejectEmail{},rejectSms{};
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
        auto found=messages.find(line);if(found==messages.end())return "MISSING";
        auto value=std::move(found->second);messages.erase(found);return value;
    }
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
    auto mailbox=std::make_shared<Mailbox>();
    auto server=CreateAuthService(CreateInMemoryAuthStore(),std::make_unique<Email>(mailbox),std::make_unique<Sms>(mailbox));
    const auto port=static_cast<unsigned short>(argc>1?std::stoi(argv[1]):19241);
    if(!server->StartOnPort(port).IsOk())return 1;
    std::cout<<server->Port()<<std::endl;
    // Only this test executable exposes synthetic delivery through its private stdin/stdout pipe.
    for(std::string line;std::getline(std::cin,line)&&!line.empty();)std::cout<<mailbox->Command(line)<<std::endl;
    server->Stop();return 0;
}
