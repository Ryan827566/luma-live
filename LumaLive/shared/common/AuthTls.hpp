#pragma once
#include <cstdlib>
#include <string_view>
namespace luma::auth::tls {
inline bool DevelopmentMode(){
    const auto* mode=std::getenv("LUMALIVE_AUTH_ENV");
    return mode&&(std::string_view(mode)=="test"||std::string_view(mode)=="development");
}
}
// Windows account transport. OpenSSL 3 is loaded from the packaged absolute
// directory, never from CWD/PATH. There is no plaintext retry after TLS failure.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>

namespace luma::auth::tls {
inline std::wstring Environment(const wchar_t* name){
    const DWORD n=GetEnvironmentVariableW(name,nullptr,0);if(!n)return {};
    std::wstring value(n,L'\0');const auto used=GetEnvironmentVariableW(name,value.data(),n);
    if(!used||used>=n)return {};value.resize(used);return value;
}
inline std::filesystem::path ExecutableDirectory(){
    wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);
    return n&&n<32768?std::filesystem::path(path).parent_path():std::filesystem::path{};
}
inline std::string Utf8(const std::filesystem::path& path){
    auto bytes=path.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
using Clock=std::chrono::steady_clock;
inline bool Wait(SOCKET socket,bool write,Clock::time_point deadline,const std::atomic<bool>* active=nullptr){
    for(;;){
        const auto now=Clock::now();if(now>=deadline||(active&&!active->load()))return false;
        auto us=std::chrono::duration_cast<std::chrono::microseconds>(deadline-now).count();
        // Local shutdown does not reliably wake every Winsock/OpenSSL readiness
        // wait. Recheck the server lifecycle without extending this deadline.
        if(active&&us>200000)us=200000;
        timeval timeout{long(us/1000000),long(us%1000000)};
        fd_set ready;FD_ZERO(&ready);FD_SET(socket,&ready);
        const auto n=select(0,write?nullptr:&ready,write?&ready:nullptr,nullptr,&timeout);
        if(n>0)return Clock::now()<deadline&&(!active||active->load());
        if(n==0){if(active)continue;return false;}
        if(WSAGetLastError()!=WSAEINTR)return false;
    }
}
struct Api {
    HMODULE ssl{},crypto{};std::mutex mutex;bool ready{};
    const void* (__cdecl *TLS_client_method)(){};
    const void* (__cdecl *TLS_server_method)(){};
    void* (__cdecl *SSL_CTX_new)(const void*){};
    void (__cdecl *SSL_CTX_free)(void*){};
    long (__cdecl *SSL_CTX_ctrl)(void*,int,long,void*){};
    void (__cdecl *SSL_CTX_set_verify)(void*,int,void*){};
    void (__cdecl *SSL_CTX_set_default_passwd_cb)(void*,int (__cdecl *)(char*,int,int,void*)){};
    int (__cdecl *SSL_CTX_load_verify_locations)(void*,const char*,const char*){};
    int (__cdecl *SSL_CTX_use_certificate_chain_file)(void*,const char*){};
    int (__cdecl *SSL_CTX_use_PrivateKey_file)(void*,const char*,int){};
    int (__cdecl *SSL_CTX_check_private_key)(void*){};
    void* (__cdecl *SSL_new)(void*){};
    void (__cdecl *SSL_free)(void*){};
    int (__cdecl *SSL_set_fd)(void*,int){};
    void* (__cdecl *SSL_get0_param)(void*){};
    int (__cdecl *X509_VERIFY_PARAM_set1_ip_asc)(void*,const char*){};
    long (__cdecl *SSL_get_verify_result)(const void*){};
    int (__cdecl *SSL_connect)(void*){};
    int (__cdecl *SSL_accept)(void*){};
    int (__cdecl *SSL_read)(void*,void*,int){};
    int (__cdecl *SSL_write)(void*,const void*,int){};
    int (__cdecl *SSL_pending)(const void*){};
    int (__cdecl *SSL_has_pending)(const void*){};
    int (__cdecl *SSL_get_error)(const void*,int){};
    void (__cdecl *ERR_clear_error)(){};
    template<class F>bool Symbol(HMODULE module,F& fn,const char* name){fn=reinterpret_cast<F>(GetProcAddress(module,name));return fn!=nullptr;}
    bool Load(){
        std::lock_guard lock(mutex);if(ready)return true;
        auto directory=std::filesystem::path(Environment(L"LUMALIVE_AUTH_TLS_LIBRARY_DIR"));
        if(directory.empty())directory=ExecutableDirectory();if(!directory.is_absolute())return false;
        constexpr auto flags=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32;
        crypto=LoadLibraryExW((directory/L"libcrypto-3-x64.dll").c_str(),nullptr,flags);
        ssl=LoadLibraryExW((directory/L"libssl-3-x64.dll").c_str(),nullptr,flags);
        if(!crypto||!ssl){if(ssl)FreeLibrary(ssl);if(crypto)FreeLibrary(crypto);ssl=crypto=nullptr;return false;}
#define LUMA_SSL_SYMBOL(name) Symbol(ssl,name,#name)
        ready=LUMA_SSL_SYMBOL(TLS_client_method)&&LUMA_SSL_SYMBOL(TLS_server_method)&&
            LUMA_SSL_SYMBOL(SSL_CTX_new)&&LUMA_SSL_SYMBOL(SSL_CTX_free)&&LUMA_SSL_SYMBOL(SSL_CTX_ctrl)&&
            LUMA_SSL_SYMBOL(SSL_CTX_set_verify)&&LUMA_SSL_SYMBOL(SSL_CTX_set_default_passwd_cb)&&LUMA_SSL_SYMBOL(SSL_CTX_load_verify_locations)&&
            LUMA_SSL_SYMBOL(SSL_CTX_use_certificate_chain_file)&&LUMA_SSL_SYMBOL(SSL_CTX_use_PrivateKey_file)&&
            LUMA_SSL_SYMBOL(SSL_CTX_check_private_key)&&LUMA_SSL_SYMBOL(SSL_new)&&LUMA_SSL_SYMBOL(SSL_free)&&
            LUMA_SSL_SYMBOL(SSL_set_fd)&&LUMA_SSL_SYMBOL(SSL_get0_param)&&LUMA_SSL_SYMBOL(SSL_get_verify_result)&&
            LUMA_SSL_SYMBOL(SSL_connect)&&LUMA_SSL_SYMBOL(SSL_accept)&&LUMA_SSL_SYMBOL(SSL_read)&&
            LUMA_SSL_SYMBOL(SSL_write)&&LUMA_SSL_SYMBOL(SSL_pending)&&LUMA_SSL_SYMBOL(SSL_has_pending)&&LUMA_SSL_SYMBOL(SSL_get_error)&&
            Symbol(crypto,X509_VERIFY_PARAM_set1_ip_asc,"X509_VERIFY_PARAM_set1_ip_asc")&&Symbol(crypto,ERR_clear_error,"ERR_clear_error");
#undef LUMA_SSL_SYMBOL
        if(!ready){FreeLibrary(ssl);FreeLibrary(crypto);ssl=crypto=nullptr;}
        // Successful modules remain loaded while any context/channel may exist.
        return ready;
    }
    static Api& Get(){static Api instance;return instance;}
};
class Context {
    void* value_{};
public:
    ~Context(){if(value_)Api::Get().SSL_CTX_free(value_);}
    Context()=default;Context(const Context&)=delete;Context& operator=(const Context&)=delete;
    void* Get()const{return value_;}
    static std::shared_ptr<Context> Create(bool server,const std::filesystem::path& certificate,const std::filesystem::path& key={}){
        auto& api=Api::Get();if(!api.Load()||!certificate.is_absolute()||(server&&!key.is_absolute()))return {};
        auto context=std::make_shared<Context>();context->value_=api.SSL_CTX_new(server?api.TLS_server_method():api.TLS_client_method());
        // SSL_CTRL_SET_MIN_PROTO_VERSION=123, TLS1_2_VERSION=0x0303 (OpenSSL3 ABI).
        if(!context->value_||api.SSL_CTX_ctrl(context->value_,123,0x0303,nullptr)!=1)return {};
        // A daemon must reject encrypted keys instead of prompting on stdin.
        api.SSL_CTX_set_default_passwd_cb(context->value_,[](char*,int,int,void*)->int{return 0;});
        const auto cert=Utf8(certificate);
        if(server){const auto secret=Utf8(key);if(api.SSL_CTX_use_certificate_chain_file(context->value_,cert.c_str())!=1||api.SSL_CTX_use_PrivateKey_file(context->value_,secret.c_str(),1)!=1||api.SSL_CTX_check_private_key(context->value_)!=1)return {};}
        else{api.SSL_CTX_set_verify(context->value_,1,nullptr);if(api.SSL_CTX_load_verify_locations(context->value_,cert.c_str(),nullptr)!=1)return {};}
        return context;
    }
};
class Channel {
    std::shared_ptr<Context> context_;void* value_{};SOCKET socket_{INVALID_SOCKET};
    const std::atomic<bool>* active_{};
    template<class Operation>int Run(Operation op,Clock::time_point deadline){
        auto& api=Api::Get();
        while(Clock::now()<deadline&&(!active_||active_->load())){
            api.ERR_clear_error();const int n=op();if(n>0)return Clock::now()<deadline?n:-1;
            const auto error=api.SSL_get_error(value_,n);
            // Either read or write can request the opposite direction.
            if((error!=2&&error!=3)||!Wait(socket_,error==3,deadline,active_))return -1;
        }
        return -1;
    }
public:
    ~Channel(){if(value_)Api::Get().SSL_free(value_);}
    Channel()=default;Channel(const Channel&)=delete;Channel& operator=(const Channel&)=delete;
    bool Open(std::shared_ptr<Context> context,SOCKET socket,bool server,const std::string& expectedIp={},const std::atomic<bool>* active=nullptr){
        if(value_||!context||socket>SOCKET(std::numeric_limits<int>::max()))return false;
        context_=std::move(context);socket_=socket;active_=active;auto& api=Api::Get();value_=api.SSL_new(context_->Get());
        if(!value_||api.SSL_set_fd(value_,int(socket))!=1)return false;
        if(!server&&(expectedIp.empty()||api.X509_VERIFY_PARAM_set1_ip_asc(api.SSL_get0_param(value_),expectedIp.c_str())!=1))return false;
        u_long nonblocking=1;if(ioctlsocket(socket,FIONBIO,&nonblocking)!=0)return false;
        if(Run([&]{return server?api.SSL_accept(value_):api.SSL_connect(value_);},Clock::now()+std::chrono::seconds(5))!=1)return false;
        return server||api.SSL_get_verify_result(value_)==0;
    }
    int Read(void* data,int size,Clock::time_point deadline){return Run([&]{return Api::Get().SSL_read(value_,data,size);},deadline);}
    int Write(const void* data,int size,Clock::time_point deadline){return Run([&]{return Api::Get().SSL_write(value_,data,size);},deadline);}
    bool Pending()const{return value_&&(Api::Get().SSL_pending(value_)>0||Api::Get().SSL_has_pending(value_)==1);}
};
}
#endif
