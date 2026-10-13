#include "AccountConnectionSettings.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

using namespace luma::client::ui::preview;
namespace fs=std::filesystem;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string Read(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void Write(const fs::path& path,const std::string& value){
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(value.data(),value.size());Require(bool(file),"write fixture");
}
bool Defaults(const AccountConnectionSettings& settings){return settings.host=="127.0.0.1"&&settings.port==9100;}
struct EnvironmentRestore {
    const wchar_t* name;std::wstring value;bool existed;
    explicit EnvironmentRestore(const wchar_t* key):name(key){
        const auto count=GetEnvironmentVariableW(name,nullptr,0);existed=count!=0;
        if(count){value.resize(count);value.resize(GetEnvironmentVariableW(name,value.data(),count));}
    }
    ~EnvironmentRestore(){SetEnvironmentVariableW(name,existed?value.c_str():nullptr);}
};
}

int main(){
    EnvironmentRestore overridePath(L"LUMALIVE_ACCOUNT_SETTINGS_PATH"),localAppData(L"LOCALAPPDATA"),language(L"LUMALIVE_UI_LANGUAGE");
    SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",L"en-US");
    fs::path root;
    std::wstring error;
    try{
        const auto parent=fs::temp_directory_path();
        for(int i=0;i<32;++i){
            const auto candidate=parent/(L"LumaAccountSettingsTests-"+std::to_wstring(GetCurrentProcessId())+
                L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(i));
            if(fs::create_directory(candidate)){root=fs::canonical(candidate);break;}
        }
        Require(!root.empty(),"create isolated test directory");
        // The only recursive cleanup target is this newly created, absolute test directory.
        struct Cleanup {fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{root};
        const auto local=root/L"local-app-data",file=local/L"LumaLive"/L"account-connection.conf";
        SetEnvironmentVariableW(L"LOCALAPPDATA",local.c_str());
        SetEnvironmentVariableW(L"LUMALIVE_ACCOUNT_SETTINGS_PATH",nullptr);
        AccountConnectionSettings settings{"10.0.0.9",9999};error=L"old error";
        Require(LoadAccountConnectionSettings(settings,error)&&Defaults(settings)&&error.empty(),"missing file uses defaults and clears error");
        Require(!fs::exists(local),"loading does not create directories");
        const AccountConnectionSettings first{"192.168.50.12",19241};
        Require(SaveAccountConnectionSettings(first,error)&&error.empty(),"save default current-user path");
        const auto firstBytes=Read(file);
        Require(firstBytes=="LumaLiveAccountConnection 1\nhost=192.168.50.12\nport=19241\n","file stores only version, host and port");
        Require(LoadAccountConnectionSettings(settings,error)&&settings.host==first.host&&settings.port==first.port,"saved settings roundtrip");
        const auto isolated=root/L"隔离目录"/L"connection.conf";
        SetEnvironmentVariableW(L"LUMALIVE_ACCOUNT_SETTINGS_PATH",isolated.c_str());
        Require(LoadAccountConnectionSettings(settings,error)&&Defaults(settings),"override never reads current-user settings");
        const AccountConnectionSettings second{"10.20.30.40",65535};
        Require(SaveAccountConnectionSettings(second,error)&&fs::exists(isolated),"environment override supports Unicode and creates parent");
        Require(Read(file)==firstBytes,"override does not change current-user file");
        Require(LoadAccountConnectionSettings(settings,error)&&settings.host==second.host&&settings.port==65535,"load environment override");
        const auto explicitFile=root/L"explicit.conf";
        Require(SaveAccountConnectionSettings(first,error,explicitFile),"explicit path save");
        Require(LoadAccountConnectionSettings(settings,error,explicitFile)&&settings.host==first.host,"explicit path takes precedence over environment");

        const std::string prefix="LumaLiveAccountConnection 1\n";
        std::vector<std::string> invalid={"",std::string(513,'x'),"LumaLiveAccountConnection 2\nhost=127.0.0.1\nport=9100\n",
            prefix+"host=127.0.0.1\n",prefix+"port=9100\nhost=127.0.0.1\n",prefix+"host=127.0.0.1\nport=9100\nextra=value\n",
            prefix+"host=127.0.0.1\nport=9100\n\n"};
        for(const auto& host:{"localhost","127.1","256.0.0.1","127.0.0.01","01.2.3.4","1.2.3.4.5","1..3.4","1.2.3.","+1.2.3.4"," 1.2.3.4","1.2.3.4 ","::1"})
            invalid.push_back(prefix+"host="+host+"\nport=9100\n");
        for(const auto& port:{"0","65536","-1","+9100","09100","9100x"," 9100","999999999999999999",""})
            invalid.push_back(prefix+"host=127.0.0.1\nport="+port+"\n");
        invalid.push_back(prefix+std::string("host=127.0.0.1\0\nport=9100\n",27));
        for(const auto& bytes:invalid){
            Write(explicitFile,bytes);settings=second;error.clear();
            Require(!LoadAccountConnectionSettings(settings,error,explicitFile)&&Defaults(settings)&&!error.empty(),"malformed settings rejected with defaults and error");
            Require(Read(explicitFile)==bytes,"invalid load does not overwrite file");
        }
        Write(explicitFile,"LumaLiveAccountConnection 1\r\nhost=0.0.0.0\r\nport=1\r\n");
        Require(LoadAccountConnectionSettings(settings,error,explicitFile)&&settings.host=="0.0.0.0"&&settings.port==1,"CRLF and lower numeric boundaries");
        Write(explicitFile,prefix+"host=255.255.255.255\nport=65535");
        Require(LoadAccountConnectionSettings(settings,error,explicitFile)&&settings.host=="255.255.255.255"&&settings.port==65535,"upper numeric boundaries without final newline");
        Require(SaveAccountConnectionSettings(first,error,explicitFile),"restore valid fixture");
        const auto retained=Read(explicitFile);
        for(const auto& bad:std::vector<AccountConnectionSettings>{{"127.0.0.01",9100},{"localhost",9100},{"1.2.3.4\nport=5",9100},{std::string(600,'1'),9100},{"127.0.0.1",0}}){
            Require(!SaveAccountConnectionSettings(bad,error,explicitFile)&&!error.empty(),"invalid save rejected");
            Require(Read(explicitFile)==retained,"invalid save preserves previous bytes");
        }
        // Deny delete sharing so replacement fails after the temporary file was written.
        HANDLE locked=CreateFileW(explicitFile.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(locked!=INVALID_HANDLE_VALUE,"lock original settings");
        const bool saved=SaveAccountConnectionSettings(second,error,explicitFile);
        CloseHandle(locked);
        Require(!saved&&!error.empty()&&Read(explicitFile)==retained,"failed atomic replacement preserves previous bytes");
        for(const auto& entry:fs::directory_iterator(root))Require(entry.path().extension()!=L".tmp","failed save removes temporary file");
        Require(SaveAccountConnectionSettings(second,error,explicitFile)&&error.empty(),"replacement succeeds after original is unlocked");
        Require(LoadAccountConnectionSettings(settings,error,explicitFile)&&settings.host==second.host,"replacement remains readable");
        const auto blocked=root/L"not-a-directory";Write(blocked,"keep me");
        Require(!SaveAccountConnectionSettings(first,error,blocked/L"connection.conf")&&Read(blocked)=="keep me","file ancestor preserved on failed save");
        SetEnvironmentVariableW(L"LUMALIVE_ACCOUNT_SETTINGS_PATH",L"relative.conf");
        Require(!SaveAccountConnectionSettings(first,error)&&!error.empty(),"relative environment override is rejected");
        Require(!LoadAccountConnectionSettings(settings,error)&&Defaults(settings),"invalid path loads defaults");
        SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",L"zh-CN");
        Require(!SaveAccountConnectionSettings({"invalid",9100},error,explicitFile)&&error.find(L"请输入有效")!=std::wstring::npos,"save error is localized");
        Write(explicitFile,"broken");
        Require(!LoadAccountConnectionSettings(settings,error,explicitFile)&&Defaults(settings)&&error.find(L"格式无效")!=std::wstring::npos,"load error is localized");
        std::cout<<"PASS: account endpoint persistence, isolated paths, strict validation, bounded reads, localized errors and atomic failure preservation\n";
    }catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<'\n';return 1;}
    return 0;
}
