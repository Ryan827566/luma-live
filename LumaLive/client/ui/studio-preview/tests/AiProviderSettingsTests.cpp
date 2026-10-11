#include "AiProviderSettings.hpp"
#include <iostream>
using namespace luma::client::ui::preview;
int wmain(int argc,wchar_t** argv){
 AiProviderSettings s{L"https://example.invalid/v1",L"fixture-secret",L"asr-fixture",L"chat-fixture",L"",L""};std::wstring error;
 if(argc==3&&std::wstring(argv[1])==L"--write-fixture")return SaveAiProviderSettings(s,error,argv[2])?0:1;
 wchar_t temporary[32768]{};if(!GetTempPathW(32768,temporary))return 1;GUID guid{};CoCreateGuid(&guid);wchar_t id[40]{};StringFromGUID2(guid,id,40);auto file=std::filesystem::path(temporary)/(std::wstring(L"luma-settings-")+id+L".dat");
 try{auto check=[](bool ok){if(!ok)throw std::runtime_error("settings regression failed");};AiProviderSettings loaded;
 check(LoadAiProviderSettings(loaded,error,file)&&loaded.apiKey.empty());check(SaveAiProviderSettings(s,error,file));check(LoadAiProviderSettings(loaded,error,file)&&loaded.apiKey==s.apiKey&&loaded.baseUrl==s.baseUrl);
 std::ifstream input(file,std::ios::binary);std::string encrypted((std::istreambuf_iterator<char>(input)),{});input.close();check(encrypted.find("fixture-secret")==encrypted.npos);
 auto invalid=s;invalid.apiKey=L"line\nbreak";check(!SaveAiProviderSettings(invalid,error,file));check(LoadAiProviderSettings(loaded,error,file)&&loaded.apiKey==s.apiKey);
 for(auto url:{L"http://example.com/v1",L"https://user:password@example.com",L"https://example.com/v1?key=secret",L"https://example.com/#",L"https://example.com/has space"}){invalid=s;invalid.baseUrl=url;check(!SaveAiProviderSettings(invalid,error,file));}
 invalid=s;invalid.ttsModel=L"speech";check(!SaveAiProviderSettings(invalid,error,file));
 std::ofstream corrupt(file,std::ios::binary|std::ios::trunc);corrupt<<"broken";corrupt.close();check(!LoadAiProviderSettings(loaded,error,file)&&loaded.apiKey.empty());
 DeleteFileW(file.c_str());std::cout<<"PASS: encrypted settings roundtrip, validation, unchanged previous save and corruption\n";return 0;
 }catch(...){DeleteFileW(file.c_str());std::cerr<<"Settings regression failed\n";return 1;}
}
