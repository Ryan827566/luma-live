#pragma once
#include "UiLocale.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <set>
#include <algorithm>
#include <cwctype>
#include <objbase.h>
#include <vector>
namespace luma::client::ui::preview {
struct AiProviderSettings { std::wstring baseUrl,apiKey,transcribeModel,chatModel,ttsModel,ttsVoice; };
namespace ProviderSettingsDetail {
constexpr size_t Limit=65536;
inline auto Fields(AiProviderSettings& s){return std::array<std::wstring*,6>{&s.baseUrl,&s.apiKey,&s.transcribeModel,&s.chatModel,&s.ttsModel,&s.ttsVoice};}
inline const char* Names[]={"BASE_URL","API_KEY","TRANSCRIBE_MODEL","CHAT_MODEL","TTS_MODEL","TTS_VOICE"};
inline bool Fail(std::wstring& error,const wchar_t* zh,const wchar_t* en){error=UiLabel(zh,en);return false;}
inline std::filesystem::path Path(const std::filesystem::path& custom){if(!custom.empty())return custom;wchar_t local[32768]{};auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);if(!n||n>=32768)return {};return std::filesystem::path(local)/L"LumaLive"/L"ai-provider.dat";}
inline bool Valid(AiProviderSettings s,std::wstring& error){
 auto fields=Fields(s);for(size_t i=0;i<fields.size();++i){const auto& v=*fields[i];if((i<4&&v.empty())||v.size()>8192||v.find_first_of(L"\r\n")!=v.npos||std::any_of(v.begin(),v.end(),[](wchar_t c){return c<32||c==127;}))return Fail(error,L"请完整填写服务地址、密钥和模型，字段不能包含换行。",L"Enter the service URL, key and models. Fields cannot contain control characters.");}
 URL_COMPONENTS url{};url.dwStructSize=sizeof(url);url.dwSchemeLength=url.dwHostNameLength=url.dwUserNameLength=url.dwPasswordLength=url.dwUrlPathLength=url.dwExtraInfoLength=DWORD(-1);
 if(!WinHttpCrackUrl(s.baseUrl.c_str(),DWORD(s.baseUrl.size()),0,&url))return Fail(error,L"服务地址格式无效。",L"Invalid service URL.");
 std::wstring host(url.lpszHostName,url.dwHostNameLength);std::transform(host.begin(),host.end(),host.begin(),[](wchar_t c){return wchar_t(towlower(c));});
 const bool local=host==L"localhost"||host==L"127.0.0.1"||host==L"::1"||host==L"[::1]";
 if(s.baseUrl.find_first_of(L"?#\\")!=s.baseUrl.npos||std::any_of(s.baseUrl.begin(),s.baseUrl.end(),[](wchar_t c){return iswspace(c)!=0;})||host.find(L'%')!=host.npos||host.empty()||!url.nPort||url.dwUserNameLength||url.dwPasswordLength||url.dwExtraInfoLength||!(url.nScheme==INTERNET_SCHEME_HTTPS||(url.nScheme==INTERNET_SCHEME_HTTP&&local)))return Fail(error,L"请使用 HTTPS 地址；仅本机服务允许 HTTP，地址不能包含账号或查询参数。",L"Use HTTPS (HTTP only for loopback). Omit credentials, query and fragment.");
 if(s.ttsModel.empty()!=s.ttsVoice.empty())return Fail(error,L"朗读模型与音色需同时填写，或同时留空。",L"Set both speech model and voice, or leave both empty.");
 return true;
}
struct Plaintext {std::string value;~Plaintext(){if(!value.empty())SecureZeroMemory(value.data(),value.size());}};
struct Blob {DATA_BLOB value{};~Blob(){if(value.pbData){SecureZeroMemory(value.pbData,value.cbData);LocalFree(value.pbData);}}};
}
inline bool SaveAiProviderSettings(const AiProviderSettings& settings,std::wstring& error,const std::filesystem::path& custom={}){
 using namespace ProviderSettingsDetail;error.clear();try{
 if(!Valid(settings,error))return false;auto copy=settings;Plaintext plain;auto fields=Fields(copy);
 for(size_t i=0;i<fields.size();++i){auto& v=*fields[i];int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),int(v.size()),nullptr,0,nullptr,nullptr);if(n==0&&!v.empty())return Fail(error,L"设置包含无效文字。",L"Settings contain invalid text.");std::string encoded(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),int(v.size()),encoded.data(),n,nullptr,nullptr);plain.value+=std::string(Names[i])+"="+encoded+"\n";if(!encoded.empty())SecureZeroMemory(encoded.data(),encoded.size());}
 if(plain.value.size()>Limit/2)return Fail(error,L"设置内容过长。",L"Settings are too large.");DATA_BLOB input{DWORD(plain.value.size()),reinterpret_cast<BYTE*>(plain.value.data())};Blob encrypted;
 if(!CryptProtectData(&input,L"LumaLive AI provider",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.value))return Fail(error,L"无法加密设置，请稍后重试。",L"Could not encrypt settings. Try again.");
 const auto file=Path(custom);if(file.empty())return Fail(error,L"找不到用户设置目录。",L"User settings directory is unavailable.");std::filesystem::create_directories(file.parent_path());GUID guid{};if(FAILED(CoCreateGuid(&guid)))return Fail(error,L"无法创建设置文件。",L"Could not create settings file.");wchar_t id[40]{};StringFromGUID2(guid,id,40);auto temp=file;temp+=std::wstring(L".")+id+L".tmp";
 HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return Fail(error,L"无法写入设置目录。",L"Could not write settings directory.");DWORD wrote=0;bool ok=WriteFile(h,encrypted.value.pbData,encrypted.value.cbData,&wrote,nullptr)&&wrote==encrypted.value.cbData&&FlushFileBuffers(h);CloseHandle(h);
 if(ok)ok=MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;if(!ok){DeleteFileW(temp.c_str());return Fail(error,L"保存失败，原设置已保留。",L"Save failed. Previous settings were retained.");}return true;
 }catch(...){return Fail(error,L"无法保存设置，请检查目录权限。",L"Could not save settings. Check directory permissions.");}
}
inline bool LoadAiProviderSettings(AiProviderSettings& settings,std::wstring& error,const std::filesystem::path& custom={}){
 using namespace ProviderSettingsDetail;settings={};error.clear();try{
 const auto file=Path(custom);if(file.empty())return Fail(error,L"找不到用户设置目录。",L"User settings directory is unavailable.");if(!std::filesystem::exists(file))return true;const auto size=std::filesystem::file_size(file);if(!size||size>Limit)return Fail(error,L"已保存的设置损坏，请重新填写。",L"Saved settings are invalid. Enter them again.");std::ifstream in(file,std::ios::binary);std::vector<BYTE> bytes(size);if(!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size)))return Fail(error,L"无法读取已保存的设置。",L"Could not read saved settings.");DATA_BLOB source{DWORD(bytes.size()),bytes.data()};Blob decoded;
 if(!CryptUnprotectData(&source,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&decoded.value)||decoded.value.cbData>Limit)return Fail(error,L"无法解密已保存的设置，请使用保存时的 Windows 用户。",L"Could not decrypt settings. Use the Windows account that saved them.");
 Plaintext plain;plain.value.assign(reinterpret_cast<char*>(decoded.value.pbData),decoded.value.cbData);AiProviderSettings result;auto fields=Fields(result);std::set<size_t> seen;size_t at=0;
 while(at<plain.value.size()){auto end=plain.value.find('\n',at);if(end==plain.value.npos)end=plain.value.size();auto line=plain.value.substr(at,end-at);at=end+1;if(line.empty())continue;auto eq=line.find('=');bool known=false;for(size_t i=0;i<fields.size();++i){if(line.substr(0,eq)!=Names[i])continue;if(eq==line.npos||!seen.insert(i).second)throw std::runtime_error("invalid");auto value=line.substr(eq+1);int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);if(!n&&!value.empty())throw std::runtime_error("invalid");fields[i]->resize(n);if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),fields[i]->data(),n);known=true;break;}if(!known)throw std::runtime_error("invalid");}
 if(seen.size()!=6||!Valid(result,error)){settings={};return Fail(error,L"已保存的设置损坏，请重新填写。",L"Saved settings are invalid. Enter them again.");}settings=std::move(result);return true;
 }catch(...){settings={};return Fail(error,L"无法读取已保存的设置，请重新填写。",L"Could not read saved settings. Enter them again.");}
}
}
