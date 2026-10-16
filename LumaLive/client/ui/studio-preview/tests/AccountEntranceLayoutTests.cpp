#include "AccountPanel.hpp"
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using luma::client::ui::preview::AccountPanel;
namespace {
struct EnvironmentRestore {
    const wchar_t* name;std::wstring value;bool existed;
    explicit EnvironmentRestore(const wchar_t* key):name(key){
        const auto count=GetEnvironmentVariableW(name,nullptr,0);existed=count!=0;
        if(count){value.resize(count);value.resize(GetEnvironmentVariableW(name,value.data(),count));}
    }
    ~EnvironmentRestore(){SetEnvironmentVariableW(name,existed?value.c_str():nullptr);}
};
void Require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
std::wstring Text(HWND control){
    const int count=GetWindowTextLengthW(control);std::wstring value(size_t(count)+1,L'\0');
    value.resize(GetWindowTextW(control,value.data(),count+1));return value;
}
bool Visible(HWND control){return control&&(GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE)!=0;}
void Click(HWND panel,int id){SendMessageW(panel,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}
struct OwnedHost {
    HWND value=CreateWindowExW(0,L"STATIC",L"Account entrance layout diagnostics",WS_OVERLAPPEDWINDOW,
        0,0,1024,768,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ~OwnedHost(){if(value)DestroyWindow(value);}
};
struct MeasuredControl {int id;RECT bounds;};
void TextFits(HWND control,int id,const RECT& bounds,const std::string& state){
    const auto value=Text(control);if(value.empty())return;
    const auto font=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));
    Require(font!=nullptr,state+": font missing on "+std::to_string(id));
    HDC dc=GetDC(control);Require(dc!=nullptr,state+": no measurement DC");
    const auto previous=SelectObject(dc,font);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    bool fits=true;SIZE measured{};
    if(id==2163){
        RECT text{0,0,width,0};DrawTextW(dc,value.c_str(),int(value.size()),&text,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        measured={text.right,text.bottom};fits=text.bottom<=height;
    }else{
        GetTextExtentPoint32W(dc,value.c_str(),int(value.size()),&measured);
        int insetX=0,insetY=0;
        if(id>=2160&&id<=2168){insetX=8;insetY=2;}
        const int windowDpi=int(GetDpiForWindow(control));
        if((id>=2170&&id<=2176)||id==2168){insetX=MulDiv(8,windowDpi,96);insetY=MulDiv(2,windowDpi,96);}
        if(id==2177){
            const auto stored=reinterpret_cast<UINT_PTR>(GetPropW(control,L"LumaComboDpi"));
            insetX=MulDiv(23,int(stored?stored:windowDpi),96);
        }
        fits=measured.cx<=width-2*insetX&&measured.cy<=height-2*insetY;
    }
    SelectObject(dc,previous);ReleaseDC(control,dc);
    Require(fits,state+": text clipped on "+std::to_string(id)+" measured "+std::to_string(measured.cx)+"x"+
        std::to_string(measured.cy)+" in "+std::to_string(width)+"x"+std::to_string(height));
}
void Check(AccountPanel& account,HWND panel,UINT dpi,int outerWidth,int outerHeight,const std::string& state,
    std::initializer_list<int> visibleFields,std::initializer_list<int> visibleControls,
    std::initializer_list<int> secretFields,bool notice,bool revealed){
    // Synthetic DPI exercises the same child layout and fonts. It does not claim
    // physical monitor switching. Reserve native frame plus the workspace brand.
    RECT available{0,0,MulDiv(outerWidth-16,int(dpi),96),MulDiv(outerHeight-79,int(dpi),96)};
    account.Place(available,dpi);
    RECT area{};GetClientRect(panel,&area);
    std::vector<MeasuredControl> controls;
    const std::string context=state+" "+std::to_string(dpi)+"DPI "+std::to_string(outerWidth)+"x"+std::to_string(outerHeight);
    std::vector<int> expected(visibleControls);
    for(int field:visibleFields){expected.push_back(2120+field);expected.push_back(2140+field);}
    if(notice)expected.push_back(2163);
    Require(Text(GetDlgItem(panel,2163)).empty()!=notice,context+": unexpected notice state");
    for(int id=2100;id<=2177;++id){
        auto control=GetDlgItem(panel,id);
        const bool shouldShow=std::find(expected.begin(),expected.end(),id)!=expected.end();
        Require(Visible(control)==shouldShow,context+": unexpected visibility on "+std::to_string(id));
        if(!shouldShow)continue;
        RECT bounds{};GetWindowRect(control,&bounds);MapWindowPoints(nullptr,panel,reinterpret_cast<POINT*>(&bounds),2);
        Require(bounds.left>=0&&bounds.top>=0&&bounds.left<bounds.right&&bounds.top<bounds.bottom&&bounds.right<=area.right&&bounds.bottom<=area.bottom,
            context+": bounds clipped on "+std::to_string(id)+" bottom "+std::to_string(bounds.bottom)+" > "+std::to_string(area.bottom));
        for(const auto& other:controls){
            if(id==2168&&other.id>=2120&&other.id<=2125&&bounds.left>other.bounds.left&&bounds.right<other.bounds.right&&bounds.top>other.bounds.top&&bounds.bottom<other.bounds.bottom){
                const auto margins=SendMessageW(GetDlgItem(panel,other.id),EM_GETMARGINS,0,0);
                Require(HIWORD(margins)>=other.bounds.right-bounds.left,context+": eye overlaps editable text");continue;
            }
            Require(std::min(bounds.right,other.bounds.right)<=std::max(bounds.left,other.bounds.left)||
                std::min(bounds.bottom,other.bounds.bottom)<=std::max(bounds.top,other.bounds.top),context+": overlap "+std::to_string(id)+" and "+std::to_string(other.id));
        }
        controls.push_back({id,bounds});
        // The password button paints an eye; its accessible label is not drawn.
        if((id>=2140&&id<=2145)||(id>=2160&&id!=2164&&id!=2165&&id!=2168))TextFits(control,id,bounds,context);
    }
    for(int field=0;field<6;++field){
        auto control=GetDlgItem(panel,2120+field);
        if(!Visible(control)){Require(Text(control).empty(),context+": hidden field retained text on "+std::to_string(field));continue;}
        const bool secret=std::find(secretFields.begin(),secretFields.end(),field)!=secretFields.end();
        Require((SendMessageW(control,EM_GETPASSWORDCHAR,0,0)!=0)==(secret&&!revealed),
            context+": incorrect password masking on "+std::to_string(field));
    }
    Require(!account.IsAuthenticated(),context+": layout diagnostic unexpectedly authenticated");
}
}

int main(){
    EnvironmentRestore language(L"LUMALIVE_UI_LANGUAGE"),settings(L"LUMALIVE_ACCOUNT_SETTINGS_PATH");
    try{
        wchar_t temporary[MAX_PATH]{};Require(GetTempPathW(MAX_PATH,temporary)!=0,"No temporary directory");
        const auto isolated=std::filesystem::path(temporary)/(L"luma-entrance-layout-"+std::to_wstring(GetCurrentProcessId()))/L"account.conf";
        SetEnvironmentVariableW(L"LUMALIVE_ACCOUNT_SETTINGS_PATH",isolated.c_str());
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};Require(InitCommonControlsEx(&common)!=FALSE,"Common controls failed");
        size_t checks=0;
        for(const auto* locale:{L"en-US",L"zh-CN",L"zh-TW"}){
            SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE",locale);
            for(UINT dpi:{96u,144u,192u}){
                OwnedHost host;Require(host.value!=nullptr,"Host creation failed");AccountPanel account;account.Open(host.value);
                const auto panel=FindWindowExW(host.value,nullptr,L"LumaAccountPage",nullptr);Require(panel!=nullptr,"Account page missing");
                auto check=[&](const char* state,std::initializer_list<int> fields,std::initializer_list<int> controls,
                    std::initializer_list<int> secrets={},bool notice=false,bool revealed=false){
                    for(const auto [width,height]:std::array<std::pair<int,int>,3>{{{480,560},{560,560},{960,800}}}){
                        Check(account,panel,dpi,width,height,state,fields,controls,secrets,notice,revealed);++checks;
                    }
                };
                auto allFieldsEmpty=[&](const char* state){for(int field=0;field<6;++field)
                    Require(Text(GetDlgItem(panel,2120+field)).empty(),std::string(state)+": prior field text survived on "+std::to_string(field));};
                check("password",{0,1},{2160,2168,2170,2171,2176},{1});
                for(int tab:{2100,2101,2102,2103})Click(panel,tab);
                check("hidden-tabs-ignored",{0,1},{2160,2168,2170,2171,2176},{1});
                Click(panel,2160);check("password-required",{0,1},{2160,2168,2170,2171,2176},{1},true);
                Click(panel,2176);check("more-options",{0,1},{2160,2168,2170,2171,2173,2174,2176},{1},true);
                Click(panel,2174);check("password-mfa",{0,1,2},{2160,2168,2170,2171,2173,2176},{1},true);
                SetWindowTextW(GetDlgItem(panel,2120),L"prior.login");SetWindowTextW(GetDlgItem(panel,2121),L"prior password");
                SetWindowTextW(GetDlgItem(panel,2122),L"prior recovery code");Click(panel,2168);
                check("password-mfa-visible",{0,1,2},{2160,2168,2170,2171,2173,2176},{1},true,true);
                Click(panel,2173);allFieldsEmpty("password-to-sms");
                check("sms",{0},{2161,2170,2171,2173,2174,2176,2177});
                Require(Text(GetDlgItem(panel,2177)).find(L'+')!=std::wstring::npos,"SMS calling code not selected");
                Click(panel,2174);check("sms-mfa",{0,2},{2161,2170,2171,2173,2176,2177});
                Click(panel,2176);check("sms-options-collapsed",{0,2},{2161,2170,2171,2176,2177});
                SetWindowTextW(GetDlgItem(panel,2120),L"13800138000");SetWindowTextW(GetDlgItem(panel,2122),L"old sms recovery code");
                Click(panel,2171);allFieldsEmpty("sms-to-recovery");
                check("recovery-initial",{0},{2161,2172,2175});
                Click(panel,2161);check("recovery-required",{0},{2161,2172,2175},{},true);
                SetWindowTextW(GetDlgItem(panel,2120),L"prior.recovery@example.invalid");
                Click(panel,2175);allFieldsEmpty("recovery-to-code");
                check("recovery-code",{1,2,3},{2160,2168,2172},{2,3});
                Click(panel,2160);check("recovery-code-required",{1,2,3},{2160,2168,2172},{2,3},true);
                SetWindowTextW(GetDlgItem(panel,2121),L"prior reset code");SetWindowTextW(GetDlgItem(panel,2122),L"prior new password");
                SetWindowTextW(GetDlgItem(panel,2123),L"prior new password");Click(panel,2168);
                check("recovery-password-visible",{1,2,3},{2160,2168,2172},{2,3},true,true);
                Click(panel,2172);allFieldsEmpty("recovery-to-login");
                check("recovery-back-to-login",{0,1},{2160,2168,2170,2171,2176},{1});
                Click(panel,2170);allFieldsEmpty("login-to-register");
                check("register",{0,1,3,4},{2160,2168,2172},{3,4});
                const wchar_t* values[]={L"layout.person",L"layout@example.invalid",L"",L"test password",L"different password"};
                for(int i=0;i<5;++i)SetWindowTextW(GetDlgItem(panel,2120+i),values[i]);
                Click(panel,2160);check("register-mismatch",{0,1,3,4},{2160,2168,2172},{3,4},true);
                struct RegistrationError {const char* state;int field;std::wstring value;const wchar_t* english;};
                const std::array<RegistrationError,4> invalid={{
                    {"register-invalid-username",0,L"bad name",L"Use 3–32 letters, numbers, or . _ - for your username."},
                    {"register-invalid-email",1,L"invalid@@example.invalid",L"Enter a valid email address."},
                    {"register-short-password",3,L"short",L"Your password is too short. Use a longer password."},
                    {"register-long-password",3,std::wstring(129,L'x'),L"Your password is too long. Shorten it and try again."}
                }};
                for(const auto& entry:invalid){
                    for(int i=0;i<5;++i)SetWindowTextW(GetDlgItem(panel,2120+i),values[i]);
                    SetWindowTextW(GetDlgItem(panel,2124),values[3]);
                    SetWindowTextW(GetDlgItem(panel,2120+entry.field),entry.value.c_str());
                    if(entry.field==3)SetWindowTextW(GetDlgItem(panel,2124),entry.value.c_str());
                    Click(panel,2160);
                    Require(IsWindowEnabled(GetDlgItem(panel,2160))!=FALSE,std::string(entry.state)+": local validation started a request");
                    if(wcscmp(locale,L"en-US")==0)Require(Text(GetDlgItem(panel,2163))==entry.english,std::string(entry.state)+": wrong validation message");
                    check(entry.state,{0,1,3,4},{2160,2168,2172},{3,4},true);
                }
                Click(panel,2168);check("register-password-visible",{0,1,3,4},{2160,2168,2172},{3,4},true,true);
                Require(account.CancelPendingAction(),"Registration Escape did not return to sign in");allFieldsEmpty("register-to-login");
                check("back-to-sign-in",{0,1},{2160,2168,2170,2171,2176},{1});
                Click(panel,2170);allFieldsEmpty("register-reopened");
                check("register-remasked",{0,1,3,4},{2160,2168,2172},{3,4});
                account.Close();
            }
        }
        std::cout<<"PASS: "<<checks<<" entrance layout/text checks, en/zh-CN/zh-TW, 96/144/192 synthetic DPI; no physical multi-monitor claim\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
