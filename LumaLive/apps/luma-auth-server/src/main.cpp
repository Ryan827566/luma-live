#include "IAuthService.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc,char**argv){
    std::uint16_t port=9100;
    if(argc>1){
        auto v=std::strtoul(argv[1],nullptr,10);
        if(v<1||v>65535){std::cerr<<"usage: luma_auth_server [port] [store]\n";return 2;}
        port=static_cast<std::uint16_t>(v);
    }
    auto server=luma::server::auth::CreateAuthService();
    if(argc>2){
        auto r=server->ConfigureStore(argv[2]);
        if(!r.IsOk()){std::cerr<<r.Message()<<'\n';return 1;}
    }
    auto r=server->StartOnPort(port);
    if(!r.IsOk()){std::cerr<<"failed to start auth server: "<<r.Message()<<'\n';return 1;}
    std::cout<<"LumaLive auth server started on TCP "<<server->Port()<<"\n"
             <<"Store: "<<(argc>2?argv[2]:"./luma_auth_users.db")<<"\n"
             <<"Press Enter to stop.\n";
    std::string line;std::getline(std::cin,line);server->Stop();return 0;
}
