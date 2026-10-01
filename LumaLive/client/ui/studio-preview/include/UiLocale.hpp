#pragma once
#include <windows.h>
namespace luma::client::ui::preview {
// First resource tranche: Chinese and English. Other locales currently fall back
// to English; a complete application-wide catalogue is still in progress.
inline bool ChineseUi(){return PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE;}
inline const wchar_t* UiLabel(const wchar_t* zh,const wchar_t* en){return ChineseUi()?zh:en;}
}
