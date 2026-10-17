#include "IAuthService.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc,char**argv){
    std::uint16_t port=9100;
    if(argc>1){
        const auto v=std::strtoul(argv[1],nullptr,10);
        if(v<1||v>65535){
            std::cerr<<"usage: luma_auth_server [port]\n";
            return 2;
        }
        port=static_cast<std::uint16_t>(v);
    }

    const char* auth_env=std::getenv("LUMALIVE_AUTH_ENV");
    if(!auth_env||!*auth_env){
        std::cerr<<"LUMALIVE_AUTH_ENV is required (development or production)\n";
        return 1;
    }
    const bool development=std::string(auth_env)=="development"||std::string(auth_env)=="test";
    const char* email_provider=std::getenv("LUMALIVE_EMAIL_PROVIDER");
    const char* sms_provider=std::getenv("LUMALIVE_SMS_PROVIDER");
    const char* tls_certificate=std::getenv("LUMALIVE_AUTH_TLS_CERT_FILE");
    const char* tls_key=std::getenv("LUMALIVE_AUTH_TLS_KEY_FILE");
    if(!development){
        if(!tls_certificate||!*tls_certificate||!tls_key||!*tls_key){
            std::cerr<<"LUMALIVE_AUTH_TLS_CERT_FILE and LUMALIVE_AUTH_TLS_KEY_FILE are required outside development/test\n";
            return 1;
        }
        if(!email_provider||!*email_provider||std::string(email_provider)=="development"||std::string(email_provider)=="mock"){
            std::cerr<<"a real LUMALIVE_EMAIL_PROVIDER or explicit disabled mode is required outside development/test\n";
            return 1;
        }
        if(!sms_provider||!*sms_provider||std::string(sms_provider)=="development"||std::string(sms_provider)=="mock"){
            std::cerr<<"a real LUMALIVE_SMS_PROVIDER or explicit disabled mode is required outside development/test\n";
            return 1;
        }
    }

    const char* database_url=std::getenv("LUMALIVE_AUTH_DATABASE_URL");
    if(!database_url||!*database_url){
        std::cerr<<"LUMALIVE_AUTH_DATABASE_URL is required; file-backed auth storage is disabled\n";
        return 1;
    }

    auto server=luma::server::auth::CreateAuthService();
    auto config=server->ConfigureDatabase(database_url);
    if(!config.IsOk()){
        std::cerr<<"failed to configure auth database: "<<config.Message()<<'\n';
        return 1;
    }

    auto r=server->StartOnPort(port);
    if(!r.IsOk()){
        std::cerr<<"failed to start auth server: "<<r.Message()<<'\n';
        return 1;
    }

    const char* bind_address=std::getenv("LUMALIVE_AUTH_BIND_ADDRESS");
    std::cout<<"LumaLive auth server started on "<<(tls_certificate&&*tls_certificate?"TLS ":"development plaintext TCP ")
             <<(bind_address&&*bind_address?bind_address:"127.0.0.1")<<":"<<server->Port()<<"\n"
             <<"Database: PostgreSQL (LUMALIVE_AUTH_DATABASE_URL)\n"
             <<"Press Enter to stop.\n";
    std::string line;
    std::getline(std::cin,line);
    server->Stop();
    return 0;
}
