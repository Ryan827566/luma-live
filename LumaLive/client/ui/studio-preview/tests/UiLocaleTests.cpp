#include "UiLocale.hpp"
#include "CallMessages.hpp"
#include <iostream>
using namespace luma::client::ui::preview;
bool CallMessagesWork(){
 const std::pair<const char*,const wchar_t*> cases[]={
  {"Call rejected",L"\u5df2\u62d2\u7edd\u6765\u7535"},
  {"Call rejected by remote participant",L"\u5bf9\u65b9\u62d2\u7edd\u4e86\u901a\u8bdd"},
  {"Remote participant is busy",L"\u5bf9\u65b9\u6b63\u5fd9\uff0c\u8bf7\u7a0d\u540e\u91cd\u8bd5"},
  {"Caller cancelled",L"\u5bf9\u65b9\u5df2\u53d6\u6d88\u547c\u53eb"},
  {"Remote participant ended the call",L"\u5bf9\u65b9\u5df2\u7ed3\u675f\u901a\u8bdd"}
 };
 for(const auto language:{L"zh-CN",L"zh-TW"}){
  SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",language);
  for(const auto& [input,expected]:cases)if(LocalizedCallMessage(input)!=UiLabel(expected,expected))return false;
  const auto connected=LocalizedCallMessage("Connected to Alice / \xe4\xb8\xad\xe6\x96\x87");
  if(connected!=std::wstring(UiLabel(L"\u5df2\u8fde\u63a5\uff1a",L""))+L"Alice / \u4e2d\u6587")return false;
  for(const auto input:{"Call rejected EXTRA", "Call ended EXTRA", "Offline EXTRA", "Media connection failed: PRIVATE_DIAGNOSTIC"}){
   auto text=LocalizedCallMessage(input);
   if(text.empty()||text.find(L"EXTRA")!=std::wstring::npos||text.find(L"PRIVATE_DIAGNOSTIC")!=std::wstring::npos)return false;
  }
  if(LocalizedCallMessage("Call ended EXTRA")!=LocalizedCallMessage("unknown server error"))return false;
 }
 SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",L"en-US");
 for(const auto& [input,ignored]:cases){std::string value(input);if(LocalizedCallMessage(value)!=std::wstring(value.begin(),value.end()))return false;}
 return LocalizedCallMessage("Media connection failed: details")==L"Media connection failed: details";
}
int main(){if(!CallMessagesWork())return 6;SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",L"en-US");if(std::wstring(UiLabel(L"\u4f1a\u8bae",L"Meetings"))!=L"Meetings")return 1;SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",L"zh-CN");if(UiUtf8(UiLabel(L"\u4f1a\u8bae",L"Meetings"))!="\xe4\xbc\x9a\xe8\xae\xae")return 2;SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",L"zh-TW");auto saved=UiLabel(L"\u4f1a\u8bae",L"Meetings");if(std::wstring(saved)!=L"\u6703\u8b70")return 3;for(int i=0;i<100;++i)UiLabel((L"\u8bbe\u7f6e"+std::to_wstring(i)).c_str(),L"Setting");if(std::wstring(saved)!=L"\u6703\u8b70")return 4;SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",L"fr-FR");if(std::wstring(UiLabel(L"\u4f1a\u8bae",L"Meetings"))!=L"Meetings")return 5;std::cout<<"PASS: Chinese, traditional conversion, English, pointer lifetime, fallback\n";}
