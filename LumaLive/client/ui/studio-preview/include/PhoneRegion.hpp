#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luma::client::ui::preview {
struct PhoneRegion {std::wstring iso,prefix,label;};
namespace phone_region_detail {
using GeoInfoFn=int(WINAPI*)(PWSTR,GEOTYPE,PWSTR,int);
using GeoNameFn=int(WINAPI*)(PWSTR,int);
using EnumNamesFn=BOOL(WINAPI*)(GEOCLASS,BOOL(CALLBACK*)(PWSTR,LPARAM),LPARAM);
inline bool Digits(std::wstring_view text){return !text.empty()&&std::all_of(text.begin(),text.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';});}
inline GeoInfoFn GeoInfo(){return reinterpret_cast<GeoInfoFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetGeoInfoEx"));}
inline std::wstring RegionName(){
    auto query=reinterpret_cast<GeoNameFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetUserDefaultGeoName"));
    wchar_t name[32]{};if(!query||query(name,32)<=1)return {};
    std::wstring value=name;for(auto& c:value)if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';return value;
}
inline void Add(std::vector<PhoneRegion>& regions,std::wstring iso,std::wstring prefix){
    if(iso.size()!=2||!std::all_of(iso.begin(),iso.end(),[](wchar_t c){return c>=L'A'&&c<=L'Z';})||prefix.size()<2||prefix.size()>5||prefix.front()!=L'+'||prefix[1]==L'0'||!Digits(std::wstring_view(prefix).substr(1)))return;
    if(std::any_of(regions.begin(),regions.end(),[&](const auto& r){return r.iso==iso;}))return;
    auto label=iso+L" "+prefix;regions.push_back({std::move(iso),std::move(prefix),std::move(label)});
}
inline BOOL CALLBACK Collect(PWSTR name,LPARAM data){
    // Never propagate a C++ exception through the Windows enumeration callback.
    try{
        auto query=GeoInfo();if(!query)return FALSE;
        wchar_t iso[8]{},dialing[16]{};
        if(query(name,GEO_ISO2,iso,8)>1&&query(name,GEO_DIALINGCODE,dialing,16)>1&&Digits(dialing))
            Add(*reinterpret_cast<std::vector<PhoneRegion>*>(data),iso,std::wstring(L"+")+dialing);
        return TRUE;
    }catch(...){return FALSE;}
}
}

inline std::vector<PhoneRegion> DefaultRegions(){
    std::vector<PhoneRegion> regions;
    auto enumerate=reinterpret_cast<phone_region_detail::EnumNamesFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"EnumSystemGeoNames"));
    if(enumerate)enumerate(16,phone_region_detail::Collect,reinterpret_cast<LPARAM>(&regions));
    // These stable choices also make the entrance usable on older Windows builds.
    phone_region_detail::Add(regions,L"US",L"+1");
    phone_region_detail::Add(regions,L"CN",L"+86");
    phone_region_detail::Add(regions,L"GB",L"+44");
    std::sort(regions.begin(),regions.end(),[](const auto& a,const auto& b){return a.iso<b.iso;});
    return regions;
}
inline size_t DefaultPhoneRegionIndex(const std::vector<PhoneRegion>& regions){
    const auto current=phone_region_detail::RegionName();
    auto found=std::find_if(regions.begin(),regions.end(),[&](const auto& region){return region.iso==current;});
    if(found==regions.end())found=std::find_if(regions.begin(),regions.end(),[](const auto& region){return region.iso==L"US"&&region.prefix==L"+1";});
    return found==regions.end()?0:size_t(found-regions.begin());
}
inline std::wstring NormalizePhoneNumber(std::wstring_view input,std::wstring_view selectedPrefix){
    std::wstring compact;compact.reserve(input.size());
    for(wchar_t c:input){
        if(c==L' '||c==L'\t'||c==L'\r'||c==L'\n'||c==L'-'||c==L'('||c==L')')continue;
        if((c>=L'0'&&c<=L'9')||c==L'+')compact+=c;else return {};
    }
    if(compact.empty())return {};
    std::wstring number;
    if(compact.front()==L'+')number=compact;
    else{
        if(selectedPrefix.size()<2||selectedPrefix.size()>5||selectedPrefix.front()!=L'+'||selectedPrefix[1]==L'0'||!phone_region_detail::Digits(selectedPrefix.substr(1)))return {};
        number=std::wstring(selectedPrefix)+compact;
    }
    const auto digits=std::wstring_view(number).substr(1);
    if(number.front()!=L'+'||digits.size()<7||digits.size()>15||digits.front()==L'0'||!phone_region_detail::Digits(digits))return {};
    // National trunk prefixes are region-specific; do not silently remove zeroes.
    return number;
}
}
