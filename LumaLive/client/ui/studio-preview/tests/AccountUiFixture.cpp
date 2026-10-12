// Test-only in-memory account server. No persistent accounts or delivery service.
#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include <cstdlib>
#include <iostream>
int main(int argc,char** argv){
    _putenv_s("LUMALIVE_AUTH_ENV","test");
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","development");
    _putenv_s("LUMALIVE_SMS_PROVIDER","development");
    auto server=luma::server::auth::CreateAuthService(luma::server::auth::CreateInMemoryAuthStore());
    const auto port=static_cast<unsigned short>(argc>1?std::stoi(argv[1]):19241);
    if(!server->StartOnPort(port).IsOk())return 1;
    std::cout<<server->Port()<<std::endl;
    std::string line;std::getline(std::cin,line);server->Stop();return 0;
}
