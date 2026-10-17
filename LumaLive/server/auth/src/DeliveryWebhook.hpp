#pragma once
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace luma::server::auth::delivery {
inline std::string Environment(const char* name) {
#ifdef _WIN32
    char* value=nullptr;size_t count=0;
    if(_dupenv_s(&value,&count,name)||!value)return {};
    std::string result(value);std::free(value);return result;
#else
    const auto* value=std::getenv(name);return value?value:"";
#endif
}
inline std::string Quote(std::string_view value) {
    std::string result="\"";const char hex[]="0123456789abcdef";
    for(unsigned char ch:value){
        if(ch=='"'||ch=='\\'){result+='\\';result+=char(ch);}
        else if(ch<32){result+="\\u00";result+=hex[ch>>4];result+=hex[ch&15];}
        else result+=char(ch);
    }
    return result+'"';
}
struct Endpoint {
    std::wstring host,path;unsigned short port=443;
};
inline bool ParseEndpoint(std::string_view url,Endpoint& endpoint) {
#ifdef _WIN32
    if(url.empty()||url.size()>2048||url.find_first_of("\r\n\t #?")!=url.npos)return false;
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),int(url.size()),nullptr,0);
    if(!count)return false;
    std::wstring wide(count,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),int(url.size()),wide.data(),count);
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwUserNameLength=parts.dwPasswordLength=DWORD(-1);
    if(!WinHttpCrackUrl(wide.c_str(),DWORD(wide.size()),0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS||
       !parts.dwHostNameLength||parts.dwUserNameLength||parts.dwPasswordLength)return false;
    endpoint.host.assign(parts.lpszHostName,parts.dwHostNameLength);
    endpoint.path=parts.dwUrlPathLength?std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength):L"/";
    endpoint.port=parts.nPort;return endpoint.port!=0;
#else
    return false;
#endif
}
class Webhook {
public:
    explicit Webhook(const char* prefix) {
        const std::string base(prefix);token_=Environment((base+"_WEBHOOK_TOKEN").c_str());
        configured_=ParseEndpoint(Environment((base+"_WEBHOOK_URL").c_str()),endpoint_)&&
            !token_.empty()&&token_.size()<=4096;
        for(unsigned char ch:token_)if(ch<33||ch>126)configured_=false;
    }
    ~Webhook(){
#ifdef _WIN32
        if(!token_.empty())SecureZeroMemory(token_.data(),token_.size());
#endif
    }
    Webhook(const Webhook&)=delete;Webhook& operator=(const Webhook&)=delete;
    bool Configured()const{return configured_;}
    bool Send(std::string_view channel,std::string_view recipient,std::string_view purpose,
              std::string_view code,std::chrono::seconds ttl)const {
        if(!configured_||recipient.empty()||recipient.size()>320||purpose.empty()||purpose.size()>64||
           code.empty()||code.size()>2048||ttl.count()<=0||ttl.count()>86400)return false;
#ifdef _WIN32
        struct Handle {HINTERNET value=nullptr;~Handle(){if(value)WinHttpCloseHandle(value);}};
        Handle session{WinHttpOpen(L"LumaLive-Auth/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
        // Each operation is bounded. This is not a single whole-delivery deadline.
        if(!session.value||!WinHttpSetTimeouts(session.value,5000,5000,5000,5000))return false;
        DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if(!WinHttpSetOption(session.value,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols)))return false;
        Handle connection{WinHttpConnect(session.value,endpoint_.host.c_str(),endpoint_.port,0)};
        if(!connection.value)return false;
        Handle request{WinHttpOpenRequest(connection.value,L"POST",endpoint_.path.c_str(),nullptr,
            WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
        if(!request.value)return false;
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
        if(!WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)))return false;
        // System certificate verification stays enabled. Never follow a redirect
        // with an OTP or bearer token, retry a POST, or log a provider response.
        const std::wstring bearer(token_.begin(),token_.end());
        const auto headers=L"Content-Type: application/json; charset=utf-8\r\nAuthorization: Bearer "+bearer+L"\r\n";
        const auto body="{\"version\":1,\"channel\":"+Quote(channel)+",\"to\":"+Quote(recipient)+
            ",\"purpose\":"+Quote(purpose)+",\"code\":"+Quote(code)+",\"expires_in_seconds\":"+std::to_string(ttl.count())+"}";
        if(!WinHttpSendRequest(request.value,headers.c_str(),DWORD(-1),const_cast<char*>(body.data()),
             DWORD(body.size()),DWORD(body.size()),0)||!WinHttpReceiveResponse(request.value,nullptr))return false;
        DWORD status=0,size=sizeof(status);
        return WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)&&status>=200&&status<300;
#else
        return false;
#endif
    }
private:
    bool configured_=false;Endpoint endpoint_;std::string token_;
};
}
