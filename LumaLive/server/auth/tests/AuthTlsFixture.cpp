#include "IAccountService.hpp"
#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "IEmailProvider.hpp"
#include "ISmsProvider.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::unique_ptr<luma::server::auth::IAuthService> Server(){
    // Always isolated: this fixture never opens the environment's database.
    return luma::server::auth::CreateAuthService(luma::server::auth::CreateInMemoryAuthStore(),
        luma::server::auth::CreateDevelopmentEmailProvider(),luma::server::auth::CreateDevelopmentSmsProvider());
}
}
int main(int argc,char** argv){
    try{
        Require(argc>=3,"usage: auth_tls_fixture server|client|guard port [probe|idle]");
        const auto port=static_cast<std::uint16_t>(std::stoul(argv[2]));
        const std::string mode=argv[1];
        if(mode=="guard"){
            auto server=Server();const auto result=server->StartOnPort(port);
            Require(!result.IsOk(),"production server accepted missing TLS configuration");
            Require(result.Message().find("TLS")!=std::string::npos,"production guard failed for an unrelated reason");
            std::cout<<"PASS production TLS guard\n";return 0;
        }
        if(mode=="reuse"){
            auto server=Server(); // Keep the same object and in-memory store throughout.
            for(int cycle=0;cycle<3;++cycle){
                if(cycle){
                    std::string command;std::getline(std::cin,command);
                    Require(command=="start","reuse start command missing");
                }
                Require(server->StartOnPort(port).IsOk(),"reused server start failed");
                Require(!server->StartOnPort(port).IsOk(),"duplicate start accepted");
                std::cout<<"READY "<<cycle<<' '<<server->Port()<<std::endl;
                std::string command;std::getline(std::cin,command);
                Require(command=="stop","reuse stop command missing");
                const auto started=std::chrono::steady_clock::now();
                Require(server->Stop().IsOk(),"reused server stop failed");
                Require(std::chrono::steady_clock::now()-started<std::chrono::seconds(2),"reused server stop too slow");
                Require(!server->IsRunning()&&server->Stop().IsOk(),"stop was not idempotent");
                std::cout<<"STOPPED "<<cycle<<std::endl;
            }
            // Keep the process alive for final stopped-resource measurements.
            std::string command;std::getline(std::cin,command);
            Require(command=="quit","reuse quit command missing");
            std::cout<<"STOPPED\n";return 0;
        }
        if(mode=="server"){
            auto server=Server();const auto result=server->StartOnPort(port);
            if(!result.IsOk())throw std::runtime_error(result.Message());
            std::cout<<"READY "<<server->Port()<<std::endl;
            std::string line;std::getline(std::cin,line);
            const auto started=std::chrono::steady_clock::now();
            Require(server->Stop().IsOk(),"server stop failed");
            Require(std::chrono::steady_clock::now()-started<std::chrono::seconds(2),"server stop did not interrupt sockets");
            std::cout<<"STOPPED\n";return 0;
        }
        Require(mode=="client","unknown fixture mode");
        auto client=luma::client::account::CreateAccountService();
        const auto connected=client->Connect("127.0.0.1",port);
        if(!connected.success){
            Require(!client->IsRunning()&&!client->IsAuthenticated(),"failed TLS left a live client");
            Require(client->Session().token.empty(),"failed TLS retained a session");
            std::cout<<"REJECTED "<<connected.code<<'\n';return 2;
        }
        if(argc>3&&std::string(argv[3])=="idle"){
            Require(client->Register("tls.fixture","tls@example.invalid","TLS fixture","temporary fixture password").success,"TLS register failed");
            Require(client->Login("tls.fixture","temporary fixture password").success,"TLS login failed");
            std::this_thread::sleep_for(std::chrono::seconds(61));
            Require(client->ValidateSession().success&&client->IsAuthenticated(),"authenticated TLS session failed after 61 idle seconds");
        }else{
            const auto result=client->RequestPasswordReset("missing.tls@example.invalid");
            if(!result.success){
                Require(!client->IsRunning()&&!client->IsAuthenticated(),"failed TLS frame retained a connection");
                std::cout<<"FRAME_REJECTED\n";return 3;
            }
        }
        Require(client->Stop().success&&!client->IsRunning()&&!client->IsAuthenticated(),"client stop retained state");
        Require(client->Session().token.empty(),"client stop retained a token");
        std::cout<<"PASS TLS client\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
