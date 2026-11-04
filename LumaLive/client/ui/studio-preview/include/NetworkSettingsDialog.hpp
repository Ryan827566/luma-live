#pragma once
#include "PreviewNetworkSettings.hpp"
#include "UiAccessibility.hpp"
#include "UiTheme.hpp"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <utility>

namespace luma::client::ui::preview {
// An owned, modeless native dialog. The workspace's existing message loop calls
// ProcessMessage; closing it never quits the application or saves edited values.
class NetworkSettingsDialog {
public:
    enum ControlId { Enabled=3100, Stun=3101, Turn=3102, Username=3103,
        Password=3104, Status=3105, Note=3106, LabelBase=3110 };

    ~NetworkSettingsDialog(){Close();if(font_)DeleteObject(font_);}
    NetworkSettingsDialog()=default;
    NetworkSettingsDialog(const NetworkSettingsDialog&)=delete;
    NetworkSettingsDialog& operator=(const NetworkSettingsDialog&)=delete;

    void Open(HWND owner,std::function<bool()> authorized){
        if(!authorized||!authorized())return;
        if(window_){ShowWindow(window_,SW_SHOWNORMAL);SetForegroundWindow(window_);return;}
        authorized_=std::move(authorized);
        // A blank standard dialog template; controls are native children created
        // during WM_INITDIALOG. WORD storage avoids padding after DLGTEMPLATE.
        alignas(DWORD) std::array<WORD,32> data{};
        auto* dialog=reinterpret_cast<DLGTEMPLATE*>(data.data());
        dialog->style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
        dialog->dwExtendedStyle=WS_EX_CONTROLPARENT;
        dialog->cx=320;dialog->cy=260;
        if(!CreateDialogIndirectParamW(GetModuleHandleW(nullptr),dialog,owner,Proc,reinterpret_cast<LPARAM>(this))){
            authorized_={};
            MessageBoxW(owner,UiLabel(L"无法打开网络设置，请重试。",L"Unable to open network settings. Please try again."),
                UiLabel(L"网络设置",L"Network settings"),MB_OK|MB_ICONERROR);
            return;
        }
        SizeAndCenter(owner);
        ShowWindow(window_,SW_SHOW);SetForegroundWindow(window_);SetFocus(Control(Enabled));
    }
    bool ProcessMessage(MSG& message){
        if(!window_||(message.hwnd!=window_&&!IsChild(window_,message.hwnd)))return false;
        // Keep IME confirmation in its edit control rather than submitting the
        // settings while a Chinese composition is still being confirmed.
        if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN&&GetPropW(message.hwnd,L"LumaNetworkComposing"))return false;
        return IsDialogMessageW(window_,&message)!=FALSE;
    }
    bool Owns(HWND target)const{return window_&&(target==window_||IsChild(window_,target));}
    void Close(){if(window_)DestroyWindow(window_);authorized_={};}

private:
    HWND window_{};HFONT font_{};UINT dpi_{96};
    std::function<bool()> authorized_;
    HWND Control(int id)const{return GetDlgItem(window_,id);}
    int S(int value)const{return MulDiv(value,int(dpi_),96);}
    HWND Add(int id,const wchar_t* type,const wchar_t* text,DWORD style=0){
        auto control=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,
            window_,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        return control;
    }
    void Fonts(){
        auto previous=font_;font_=UiTheme::Font(S(14));
        EnumChildWindows(window_,[](HWND child,LPARAM font)->BOOL{
            SendMessageW(child,WM_SETFONT,WPARAM(font),TRUE);return TRUE;
        },reinterpret_cast<LPARAM>(font_));
        if(previous)DeleteObject(previous);
    }
    void Place(int id,int x,int y,int w,int h){MoveWindow(Control(id),S(x),S(y),S(w),S(h),TRUE);}
    void Layout(){
        RECT bounds{};GetClientRect(window_,&bounds);const int width=MulDiv(bounds.right,96,dpi_);
        const int fieldWidth=width-48;
        Place(Enabled,24,20,fieldWidth,26);
        Place(Note,24,58,fieldWidth,74);
        for(int i=0;i<4;++i){Place(LabelBase+i,24,144+i*63,fieldWidth,20);Place(Stun+i,24,166+i*63,fieldWidth,30);}
        Place(Status,24,404,fieldWidth,58);
        Place(IDOK,width-224,472,96,34);Place(IDCANCEL,width-120,472,96,34);
    }
    void SizeAndCenter(HWND owner){
        RECT bounds{0,0,S(620),S(530)};
        AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(window_,GWL_STYLE)),FALSE,
            DWORD(GetWindowLongPtrW(window_,GWL_EXSTYLE)),dpi_);
        RECT ownerBounds{};GetWindowRect(owner,&ownerBounds);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
        const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        const int x=std::max(int(monitor.rcWork.left),std::min(int(ownerBounds.left+(ownerBounds.right-ownerBounds.left-width)/2),int(monitor.rcWork.right)-width));
        const int y=std::max(int(monitor.rcWork.top),std::min(int(ownerBounds.top+(ownerBounds.bottom-ownerBounds.top-height)/2),int(monitor.rcWork.bottom)-height));
        SetWindowPos(window_,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);Layout();
    }
    void EnableFields(){
        const bool enabled=SendMessageW(Control(Enabled),BM_GETCHECK,0,0)==BST_CHECKED;
        for(int id:{Stun,Turn,Username,Password})EnableWindow(Control(id),enabled);
    }
    void SetStatus(const std::wstring& message){SetWindowTextW(Control(Status),message.c_str());}
    static std::wstring Field(HWND control){
        const int size=GetWindowTextLengthW(control);std::wstring value(size_t(size)+1,L'\0');
        value.resize(size_t(GetWindowTextW(control,value.data(),size+1)));return value;
    }
    void Save(){
        if(!authorized_||!authorized_()){Close();return;}
        PreviewNetworkSettings settings;
        settings.enabled=SendMessageW(Control(Enabled),BM_GETCHECK,0,0)==BST_CHECKED;
        settings.stunUrl=Field(Control(Stun));settings.turnUrl=Field(Control(Turn));
        settings.turnUsername=Field(Control(Username));settings.turnPassword=Field(Control(Password));
        std::wstring error;
        const bool valid=ValidatePreviewNetworkSettings(settings,error);
        const bool saved=valid&&SavePreviewNetworkSettings(settings,error);
        if(!settings.turnPassword.empty())SecureZeroMemory(settings.turnPassword.data(),settings.turnPassword.size()*sizeof(wchar_t));
        if(!saved){
            if(!valid&&!settings.enabled)error+=UiLabel(L" 请先启用自定义设置修正填写内容，再关闭并保存。",L" Enable custom settings to correct the fields, then turn them off and save.");
            SetStatus(error.empty()?UiLabel(L"无法保存网络设置，请检查填写内容后重试。",L"Unable to save network settings. Check the fields and try again."):error);return;
        }
        SetStatus(UiLabel(L"已保存。下次新建通话或加入会议时生效，当前连接不变。",L"Saved. Applies to the next new call or meeting join. Current connections stay unchanged."));
    }
    void Initialize(){
        dpi_=GetDpiForWindow(window_);Fonts();
        SetWindowTextW(window_,UiLabel(L"通话与会议网络设置",L"Call and meeting network settings"));
        Add(Enabled,L"BUTTON",UiLabel(L"启用自定义 STUN / TURN",L"Use custom STUN / TURN"),WS_TABSTOP|BS_AUTOCHECKBOX);
        Add(Note,L"STATIC",UiLabel(
            L"通话与会议共用。未启用时沿用环境默认设置。保存仅影响下次新连接，当前连接不变。跨网络连通性仍需双方实际测试。TURN 密码使用 Windows 加密保护保存在本机。",
            L"Shared by calls and meetings. When off, environment defaults apply. Saving affects new connections only. Both participants must still test connectivity across networks. TURN passwords are protected by Windows encryption on this device."),SS_NOPREFIX);
        const wchar_t* labels[]={UiLabel(L"STUN 地址（选填）",L"STUN URL (optional)"),UiLabel(L"TURN 地址（选填）",L"TURN URL (optional)"),UiLabel(L"TURN 用户名",L"TURN username"),UiLabel(L"TURN 密码",L"TURN password")};
        for(int i=0;i<4;++i){
            Add(LabelBase+i,L"STATIC",labels[i],SS_NOPREFIX);
            auto edit=Add(Stun+i,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|(i==3?ES_PASSWORD:0));
            SendMessageW(edit,EM_SETLIMITTEXT,i<2?2048:(i==2?1024:4096),0);SetAccessibleName(edit,labels[i]);
            SetWindowSubclass(edit,EditKeys,3100,0);
        }
        SendMessageW(Control(Stun),EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"stun:stun.example.com:3478"));
        SendMessageW(Control(Turn),EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"turn:turn.example.com:3478"));
        Add(Status,L"STATIC",L"",SS_NOPREFIX);
        Add(IDOK,L"BUTTON",UiLabel(L"保存",L"Save"),WS_TABSTOP|BS_DEFPUSHBUTTON);
        Add(IDCANCEL,L"BUTTON",UiLabel(L"取消",L"Cancel"),WS_TABSTOP|BS_PUSHBUTTON);
        SendMessageW(window_,DM_SETDEFID,IDOK,0);
        PreviewNetworkSettings settings;std::wstring error;
        if(LoadPreviewNetworkSettings(settings,error)){
            SendMessageW(Control(Enabled),BM_SETCHECK,settings.enabled?BST_CHECKED:BST_UNCHECKED,0);
            SetWindowTextW(Control(Stun),settings.stunUrl.c_str());SetWindowTextW(Control(Turn),settings.turnUrl.c_str());
            SetWindowTextW(Control(Username),settings.turnUsername.c_str());SetWindowTextW(Control(Password),settings.turnPassword.c_str());
            SetStatus(UiLabel(L"修改后请保存；取消或关闭不会保存未保存的修改。",L"Save to apply your edits. Cancel or close discards unsaved changes."));
        }else SetStatus(error.empty()?UiLabel(L"无法读取已保存的设置，请重新填写或取消。",L"Unable to read saved settings. Enter them again or cancel."):error);
        if(!settings.turnPassword.empty())SecureZeroMemory(settings.turnPassword.data(),settings.turnPassword.size()*sizeof(wchar_t));
        EnableFields();Layout();
    }
    static LRESULT CALLBACK EditKeys(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
        if(message==WM_IME_STARTCOMPOSITION)SetPropW(control,L"LumaNetworkComposing",HANDLE(1));
        if(message==WM_IME_ENDCOMPOSITION)RemovePropW(control,L"LumaNetworkComposing");
        if(message==WM_NCDESTROY){RemovePropW(control,L"LumaNetworkComposing");RemoveWindowSubclass(control,EditKeys,id);}
        return DefSubclassProc(control,message,wp,lp);
    }
    static INT_PTR CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<NetworkSettingsDialog*>(GetWindowLongPtrW(window,DWLP_USER));
        if(message==WM_INITDIALOG){self=reinterpret_cast<NetworkSettingsDialog*>(lp);self->window_=window;SetWindowLongPtrW(window,DWLP_USER,lp);self->Initialize();return TRUE;}
        if(!self)return FALSE;
        switch(message){
        case WM_COMMAND:
            if(LOWORD(wp)==IDCANCEL){self->Close();return TRUE;}
            if(LOWORD(wp)==IDOK&&HIWORD(wp)==BN_CLICKED){self->Save();return TRUE;}
            if(LOWORD(wp)==Enabled&&HIWORD(wp)==BN_CLICKED)self->EnableFields();
            if((LOWORD(wp)==Enabled&&HIWORD(wp)==BN_CLICKED)||(LOWORD(wp)>=Stun&&LOWORD(wp)<=Password&&HIWORD(wp)==EN_CHANGE))
                self->SetStatus(UiLabel(L"有未保存的修改。",L"You have unsaved changes."));
            break;
        case WM_CLOSE:self->Close();return TRUE;
        case WM_SIZE:self->Layout();return TRUE;
        case WM_DPICHANGED:{self->dpi_=HIWORD(wp);self->Fonts();const auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);self->Layout();return TRUE;}
        case WM_NCDESTROY:
            self->window_=nullptr;SetWindowLongPtrW(window,DWLP_USER,0);return FALSE;
        }
        return FALSE;
    }
};
}
