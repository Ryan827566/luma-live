#pragma once
#include <windows.h>
#include <string>
namespace luma::client::ui::preview {
// First resource tranche: Chinese and English. Other locales currently fall back
// to English; a complete application-wide catalogue is still in progress.
inline bool ChineseUi(){return PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE;}
inline const wchar_t* UiLabel(const wchar_t* zh,const wchar_t* en){return ChineseUi()?zh:en;}
inline std::string UiUtf8(const wchar_t* value){
 int size=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
 if(size<=1)return {};
 std::string text(size,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),size,nullptr,nullptr);text.pop_back();return text;
}
}
