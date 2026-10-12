#pragma once
#include <windows.h>
#include <string>
#include <map>
#include <mutex>
namespace luma::client::ui::preview {
// Supported interface resources: simplified/traditional Chinese and English.
// Other Windows languages fall back to English; device/model/user text is data.
inline LANGID UiLanguage(){wchar_t name[32]{};if(GetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",name,32)){if(wcscmp(name,L"zh-CN")==0)return 0x0804;if(wcscmp(name,L"zh-TW")==0||wcscmp(name,L"zh-HK")==0)return 0x0404;if(wcscmp(name,L"en-US")==0)return 0x0409;return 0x0409;}return GetUserDefaultUILanguage();}
inline bool ChineseUi(){return PRIMARYLANGID(UiLanguage())==LANG_CHINESE;}
inline const wchar_t* UiLabel(const wchar_t* zh,const wchar_t* en){const auto lang=UiLanguage();if(PRIMARYLANGID(lang)!=LANG_CHINESE)return en;if(lang==0x0804||lang==0x1004)return zh;
 static std::mutex mutex;static std::map<std::wstring,std::wstring> traditional;std::lock_guard lock(mutex);auto found=traditional.find(zh);if(found!=traditional.end())return found->second.c_str();int count=LCMapStringEx(L"zh-TW",LCMAP_TRADITIONAL_CHINESE,zh,-1,nullptr,0,nullptr,nullptr,0);std::wstring converted;if(count>0){converted.resize(count);if(!LCMapStringEx(L"zh-TW",LCMAP_TRADITIONAL_CHINESE,zh,-1,converted.data(),count,nullptr,nullptr,0))converted=zh;else converted.pop_back();}else converted=zh;return traditional.emplace(zh,std::move(converted)).first->second.c_str();
}
inline std::string UiUtf8(const wchar_t* value){int size=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);if(size<=1)return {};std::string text(size,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),size,nullptr,nullptr);text.pop_back();return text;}
}
