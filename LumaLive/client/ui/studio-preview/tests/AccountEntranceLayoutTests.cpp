#include "AccountPanel.hpp"
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <filesystem>
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
void Check(AccountPanel& account,HWND panel,UINT dpi,int outerWidth,int outerHeight,const std::string& state){
    // Synthetic DPI exercises the same child layout and fonts. It does not claim
    // physical monitor switching. Reserve native frame plus the workspace brand.
    RECT available{0,0,MulDiv(outerWidth-16,int(dpi),96),MulDiv(outerHeight-79,int(dpi),96)};
    account.Place(available,dpi);
    RECT area{};GetClientRect(panel,&area);
    std::vector<MeasuredControl> controls;
    const std::string context=state+" "+std::to_string(dpi)+"DPI "+std::to_string(outerWidth)+"x"+std::to_string(outerHeight);
    for(int id=2100;id<=2177;++id){
        auto control=GetDlgItem(panel,id);if(!Visible(control))continue;
        RECT bounds{};GetWindowRect(control,&bounds);MapWindowPoints(nullptr,panel,reinterpret_cast<POINT*>(&bounds),2);
        Require(bounds.left>=0&&bounds.top>=0&&bounds.left<bounds.right&&bounds.top<bounds.bottom&&bounds.right<=area.right&&bounds.bottom<=area.bottom,
            context+": bounds clipped on "+std::to_string(id)+" bottom "+std::to_string(bounds.bottom)+" > "+std::to_string(area.bottom));
        for(const auto& other:controls)Require(std::min(bounds.right,other.bounds.right)<=std::max(bounds.left,other.bounds.left)||
            std::min(bounds.bottom,other.bounds.bottom)<=std::max(bounds.top,other.bounds.top),context+": overlap "+std::to_string(id)+" and "+std::to_string(other.id));
        controls.push_back({id,bounds});
        // The password button paints an eye; its accessible label is not drawn.
        if((id>=2140&&id<=2145)||(id>=2160&&id!=2164&&id!=2165&&id!=2168))TextFits(control,id,bounds,context);
    }
    for(int id:{2100,2101,2102,2103,2164,2165})Require(!Visible(GetDlgItem(panel,id)),context+": hidden entrance control exposed");
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
                auto check=[&](const char* state){for(const auto [width,height]:std::array<std::pair<int,int>,3>{{{480,560},{560,560},{960,800}}}){Check(account,panel,dpi,width,height,state);++checks;}};
                check("password");
                for(int tab:{2100,2101,2102,2103})Click(panel,tab);
                Require(Visible(GetDlgItem(panel,2121))&&!Visible(GetDlgItem(panel,2122)),"Hidden tabs changed the entrance");
                Click(panel,2160);check("password-required");
                Click(panel,2176);check("more-options");
                Click(panel,2174);check("password-mfa");
                Click(panel,2173);check("sms");
                Require(Visible(GetDlgItem(panel,2177)),"SMS calling-code selector missing");
                Require(Text(GetDlgItem(panel,2177)).find(L'+')!=std::wstring::npos,"SMS calling code not selected");
                Require(!Visible(GetDlgItem(panel,2121))&&!Visible(GetDlgItem(panel,2160)),"SMS exposed code before request");
                Click(panel,2174);check("sms-mfa");
                Click(panel,2176);check("sms-options-collapsed");
                Click(panel,2171);check("recovery-initial");
                Click(panel,2161);check("recovery-required");
                Click(panel,2175);check("recovery-code");
                Require(!Visible(GetDlgItem(panel,2120)),"Reset-code step retained the identity field");
                Click(panel,2160);check("recovery-code-required");
                Click(panel,2172);Click(panel,2170);check("register");
                Require(!Visible(GetDlgItem(panel,2122)),"Registration exposed display-name field");
                const wchar_t* values[]={L"layout.person",L"layout@example.invalid",L"",L"test password",L"different password"};
                for(int i=0;i<5;++i)SetWindowTextW(GetDlgItem(panel,2120+i),values[i]);
                Click(panel,2160);check("register-mismatch");
                Click(panel,2168);check("register-password-visible");
                Require(account.CancelPendingAction(),"Recovery Escape did not return to sign in");check("back-to-sign-in");
                account.Close();
            }
        }
        std::cout<<"PASS: "<<checks<<" entrance layout/text checks, en/zh-CN/zh-TW, 96/144/192 synthetic DPI; no physical multi-monitor claim\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
