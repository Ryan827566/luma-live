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

    std::cout<<"LumaLive auth server started on TCP "<<server->Port()<<"\n"
             <<"Database: PostgreSQL (LUMALIVE_AUTH_DATABASE_URL)\n"
             <<"Press Enter to stop.\n";
    std::string line;
    std::getline(std::cin,line);
    server->Stop();
    return 0;
}
