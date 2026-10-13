#pragma once
#include "UiLocale.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace luma::client::ui::preview {
struct AccountConnectionSettings {
    std::string host{"127.0.0.1"};
    std::uint16_t port{9100};
};

namespace account_connection_settings_detail {
inline constexpr std::size_t MaxBytes=512;
inline constexpr std::string_view Version="LumaLiveAccountConnection 1";

inline bool Fail(std::wstring& error,const wchar_t* zh,const wchar_t* en){
    error=UiLabel(zh,en);return false;
}
inline bool ValidHost(std::string_view host){
    if(host.size()<7||host.size()>15)return false;
    std::size_t start=0;
    for(int part=0;part<4;++part){
        const auto end=host.find('.',start);
        if((part<3)==(end==std::string_view::npos))return false;
        const auto octet=host.substr(start,end==std::string_view::npos?host.size()-start:end-start);
        if(octet.empty()||octet.size()>3||(octet.size()>1&&octet.front()=='0'))return false;
        unsigned value=0;
        for(char c:octet){if(c<'0'||c>'9')return false;value=value*10+unsigned(c-'0');}
        if(value>255)return false;
        start=end==std::string_view::npos?host.size():end+1;
    }
    return true;
}
inline bool Environment(const wchar_t* name,std::wstring& value){
    wchar_t buffer[32768]{};
    SetLastError(ERROR_SUCCESS);
    const auto count=GetEnvironmentVariableW(name,buffer,32768);
    if(!count){value.clear();return GetLastError()==ERROR_ENVVAR_NOT_FOUND||GetLastError()==ERROR_SUCCESS;}
    if(count>=32768)return false;
    value.assign(buffer,count);return true;
}
inline bool ResolvePath(const std::filesystem::path& custom,std::filesystem::path& path){
    if(!custom.empty())path=custom;
    else{
        std::wstring value;
        if(!Environment(L"LUMALIVE_ACCOUNT_SETTINGS_PATH",value))return false;
        if(!value.empty())path=value;
        else{
            if(!Environment(L"LOCALAPPDATA",value)||value.empty())return false;
            path=std::filesystem::path(value)/L"LumaLive"/L"account-connection.conf";
        }
    }
    return path.is_absolute()&&!path.filename().empty()&&path.filename()!=L"."&&path.filename()!=L"..";
}
inline bool Parse(std::string_view bytes,AccountConnectionSettings& settings){
    std::array<std::string_view,3> lines;
    for(auto& line:lines){
        const auto end=bytes.find('\n');
        line=bytes.substr(0,end);
        bytes=end==std::string_view::npos?std::string_view{}:bytes.substr(end+1);
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
    }
    if(!bytes.empty()||lines[0]!=Version||lines[1].substr(0,5)!="host="||lines[2].substr(0,5)!="port=")return false;
    const auto host=lines[1].substr(5),port=lines[2].substr(5);
    if(!ValidHost(host)||port.empty()||port.size()>5||(port.size()>1&&port.front()=='0'))return false;
    unsigned number=0;
    for(char c:port){if(c<'0'||c>'9')return false;number=number*10+unsigned(c-'0');}
    if(number==0||number>65535)return false;
    settings.host=host;settings.port=static_cast<std::uint16_t>(number);return true;
}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle()=default;
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile(){if(!path.empty())DeleteFileW(path.c_str());}
};
}

// Missing files use defaults. Failed loads also leave defaults and never rewrite the source file.
inline bool LoadAccountConnectionSettings(
    AccountConnectionSettings& settings,std::wstring& error,const std::filesystem::path& custom={}){
    namespace detail=account_connection_settings_detail;
    settings={};error.clear();
    try{
        std::filesystem::path path;
        if(!detail::ResolvePath(custom,path))return detail::Fail(error,
            L"无法确定账户连接设置的位置。",L"The account connection settings location is unavailable.");
        detail::Handle file;
        file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file.value==INVALID_HANDLE_VALUE){
            const auto code=GetLastError();
            if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)return true;
            return detail::Fail(error,L"无法读取账户连接设置，已使用默认值。",L"Cannot read account connection settings. Defaults are in use.");
        }
        LARGE_INTEGER size{};
        if(!GetFileSizeEx(file.value,&size)||size.QuadPart<=0||size.QuadPart>detail::MaxBytes)
            return detail::Fail(error,L"账户连接设置无效或过大，已使用默认值。原文件未修改。",L"Account connection settings are invalid or too large. Defaults are in use; the file was not changed.");
        std::array<char,detail::MaxBytes+1> bytes{};DWORD read=0;
        if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=size.QuadPart)
            return detail::Fail(error,L"无法读取账户连接设置，已使用默认值。",L"Cannot read account connection settings. Defaults are in use.");
        AccountConnectionSettings loaded;
        if(!detail::Parse(std::string_view(bytes.data(),read),loaded))return detail::Fail(error,
            L"账户连接设置格式无效，已使用默认值。原文件未修改。",L"Account connection settings have an invalid format. Defaults are in use; the file was not changed.");
        settings=std::move(loaded);return true;
    }catch(...){
        return detail::Fail(error,L"无法读取账户连接设置，已使用默认值。",L"Cannot read account connection settings. Defaults are in use.");
    }
}

// Explicit saves replace only the endpoint, never credentials, using a same-directory temporary file.
inline bool SaveAccountConnectionSettings(
    const AccountConnectionSettings& settings,std::wstring& error,const std::filesystem::path& custom={}){
    namespace detail=account_connection_settings_detail;
    error.clear();
    if(!detail::ValidHost(settings.host)||settings.port==0)return detail::Fail(error,
        L"请输入有效的 IPv4 地址和 1–65535 之间的端口。",L"Enter a valid IPv4 address and a port between 1 and 65535.");
    try{
        std::filesystem::path path;
        if(!detail::ResolvePath(custom,path))return detail::Fail(error,
            L"无法确定账户连接设置的位置。",L"The account connection settings location is unavailable.");
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(),ec);
        if(ec)return detail::Fail(error,L"无法保存账户连接设置，原设置已保留。",L"Cannot save account connection settings. Previous settings were retained.");
        const auto text=std::string(detail::Version)+"\nhost="+settings.host+"\nport="+std::to_string(settings.port)+"\n";
        if(text.size()>detail::MaxBytes)return detail::Fail(error,L"账户连接设置过大。",L"Account connection settings are too large.");
        // Destruction closes the handle before deleting a failed temporary file.
        detail::TemporaryFile temporary;
        detail::Handle file;
        static std::atomic<unsigned long long> sequence{0};
        for(int attempt=0;attempt<8;++attempt){
            const auto candidate=path.parent_path()/(L".account-connection-"+std::to_wstring(GetCurrentProcessId())+
                L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(sequence.fetch_add(1))+L".tmp");
            file.value=CreateFileW(candidate.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file.value!=INVALID_HANDLE_VALUE){temporary.path=candidate;break;}
            if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)break;
        }
        if(file.value==INVALID_HANDLE_VALUE)return detail::Fail(error,
            L"无法保存账户连接设置，原设置已保留。",L"Cannot save account connection settings. Previous settings were retained.");
        DWORD written=0;
        bool saved=WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE&&written==text.size();
        if(saved)saved=FlushFileBuffers(file.value)!=FALSE;
        const bool closed=CloseHandle(file.value)!=FALSE;file.value=INVALID_HANDLE_VALUE;
        if(saved&&closed)saved=MoveFileExW(temporary.path.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
        else saved=false;
        if(!saved)return detail::Fail(error,L"无法保存账户连接设置，原设置已保留。",L"Cannot save account connection settings. Previous settings were retained.");
        temporary.path.clear();return true;
    }catch(...){
        return detail::Fail(error,L"无法保存账户连接设置，原设置已保留。",L"Cannot save account connection settings. Previous settings were retained.");
    }
}
}
