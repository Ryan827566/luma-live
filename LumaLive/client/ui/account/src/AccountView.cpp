#include "AccountView.hpp"
#include "IAccountService.hpp"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>

namespace luma::client::ui::account {
namespace {

constexpr int ID_HOST=100,ID_PORT=101,ID_USERNAME=102,ID_EMAIL=103,ID_DISPLAY=104,ID_PASSWORD=105;
constexpr int ID_AVATAR=106,ID_VERIFY=107,ID_CURRENT_PASSWORD=108,ID_NEW_PASSWORD=109,ID_MFA=110,ID_RESET_TOKEN=111;
constexpr int ID_CONNECT=200,ID_REGISTER=201,ID_LOGIN=202,ID_LOGOUT=203,ID_REFRESH=204,ID_SAVE=205,ID_DELETE=206;
constexpr int ID_VERIFY_REQUEST=207,ID_VERIFY_EMAIL=208,ID_CHANGE_PASSWORD=209,ID_ENABLE_MFA=210,ID_DISABLE_MFA=211;
constexpr int ID_SESSIONS=212,ID_REVOKE_OTHERS=213,ID_EVENTS=214,ID_RESET_REQUEST=215,ID_RESET_PASSWORD=216;
constexpr int ID_STATUS=300;

std::wstring utf8_to_wide(const std::string& value) {
    if(value.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(n<=0) return L"";
    std::wstring out(static_cast<std::size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),n);
    return out;
}

std::string wide_to_utf8(const std::wstring& value) {
    if(value.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return "";
    std::string out(static_cast<std::size_t>(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);
    return out;
}

std::wstring GetText(HWND h) {
    const int n=GetWindowTextLengthW(h);
    std::wstring out(static_cast<std::size_t>(n)+1,L'\0');
    if(n>0) {
        GetWindowTextW(h,out.data(),n+1);
        out.resize(static_cast<std::size_t>(n));
    }
    return out;
}

void SetText(HWND window,int id,const std::string& value){
    SetWindowTextW(GetDlgItem(window,id),utf8_to_wide(value).c_str());
}

void SetStatus(HWND window,const std::wstring& value){
    SetWindowTextW(GetDlgItem(window,ID_STATUS),value.c_str());
}

void EnableConnectedButtons(HWND window,bool connected) {
    EnableWindow(GetDlgItem(window,ID_REGISTER),connected);
    EnableWindow(GetDlgItem(window,ID_LOGIN),connected);
    EnableWindow(GetDlgItem(window,ID_RESET_REQUEST),connected);
    EnableWindow(GetDlgItem(window,ID_RESET_PASSWORD),connected);
}

void EnableAuthenticatedButtons(HWND window,bool authenticated) {
    for(const int id:{
        ID_LOGOUT,ID_REFRESH,ID_SAVE,ID_DELETE,ID_VERIFY_REQUEST,ID_VERIFY_EMAIL,
        ID_CHANGE_PASSWORD,ID_ENABLE_MFA,ID_DISABLE_MFA,ID_SESSIONS,
        ID_REVOKE_OTHERS,ID_EVENTS
    }) {
        EnableWindow(GetDlgItem(window,id),authenticated);
    }
}

class Window {
public:
    explicit Window(HINSTANCE instance)
        :instance_(instance),service_(luma::client::account::CreateAccountService()){}

    int Run(int show) {
        WNDCLASSW wc{};
        wc.lpfnWndProc=&Window::Proc;
        wc.hInstance=instance_;
        wc.lpszClassName=L"LumaLiveAccountWindow";
        wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        RegisterClassW(&wc);

        window_=CreateWindowExW(
            0,wc.lpszClassName,L"LumaLive 账号与安全中心",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,760,840,nullptr,nullptr,instance_,this);
        if(!window_)return 1;

        ShowWindow(window_,show);
        UpdateWindow(window_);

        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    void AddLabel(const wchar_t* text,int x,int y,int w) {
        CreateWindowW(L"STATIC",text,WS_CHILD|WS_VISIBLE,x,y,w,24,window_,nullptr,instance_,nullptr);
    }

    HWND AddEdit(int id,const wchar_t* value,int x,int y,int w,bool password=false) {
        return CreateWindowExW(
            WS_EX_CLIENTEDGE,L"EDIT",value,
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|(password?ES_PASSWORD:0),
            x,y,w,28,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);
    }

    HWND AddButton(int id,const wchar_t* text,int x,int y,int w) {
        return CreateWindowW(
            L"BUTTON",text,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x,y,w,32,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);
    }

    void CreateControls() {
        AddLabel(L"认证服务器",24,20,100);
        AddEdit(ID_HOST,L"127.0.0.1",130,16,320);
        AddLabel(L"端口",470,20,40);
        AddEdit(ID_PORT,L"9100",510,16,70);

        AddLabel(L"用户名 / 邮箱",24,62,100);
        AddEdit(ID_USERNAME,L"",130,58,520);

        AddLabel(L"邮箱",24,104,100);
        AddEdit(ID_EMAIL,L"",130,100,520);

        AddLabel(L"显示名称",24,146,100);
        AddEdit(ID_DISPLAY,L"",130,142,520);

        AddLabel(L"密码",24,188,100);
        AddEdit(ID_PASSWORD,L"",130,184,520,true);

        AddLabel(L"头像 URL",24,230,100);
        AddEdit(ID_AVATAR,L"",130,226,520);

        AddLabel(L"邮箱验证码 / Token",24,272,140);
        AddEdit(ID_VERIFY,L"",170,268,480);

        AddLabel(L"当前密码",24,314,100);
        AddEdit(ID_CURRENT_PASSWORD,L"",130,310,520,true);

        AddLabel(L"新密码",24,356,100);
        AddEdit(ID_NEW_PASSWORD,L"",130,352,520,true);

        AddLabel(L"MFA / 恢复码",24,398,120);
        AddEdit(ID_MFA,L"",170,394,480);

        AddLabel(L"密码重置 Token",24,440,140);
        AddEdit(ID_RESET_TOKEN,L"",170,436,480);

        AddButton(ID_CONNECT,L"连接服务器",24,482,120);
        AddButton(ID_REGISTER,L"注册账号",154,482,100);
        AddButton(ID_LOGIN,L"登录",264,482,90);
        AddButton(ID_LOGOUT,L"退出登录",364,482,100);

        AddButton(ID_REFRESH,L"刷新资料",24,522,110);
        AddButton(ID_SAVE,L"保存资料",144,522,110);
        AddButton(ID_DELETE,L"删除账号",264,522,110);

        AddButton(ID_VERIFY_REQUEST,L"请求邮箱验证",384,522,120);
        AddButton(ID_VERIFY_EMAIL,L"验证邮箱",514,522,100);

        AddButton(ID_CHANGE_PASSWORD,L"修改密码",24,562,110);
        AddButton(ID_ENABLE_MFA,L"启用 MFA",144,562,100);
        AddButton(ID_DISABLE_MFA,L"关闭 MFA",254,562,100);
        AddButton(ID_SESSIONS,L"查看登录设备",364,562,120);
        AddButton(ID_REVOKE_OTHERS,L"撤销其他设备",494,562,120);
        AddButton(ID_EVENTS,L"安全审计",624,562,90);

        AddButton(ID_RESET_REQUEST,L"请求密码重置",24,602,120);
        AddButton(ID_RESET_PASSWORD,L"使用 Token 重置",154,602,120);

        CreateWindowW(
            L"STATIC",L"未连接",WS_CHILD|WS_VISIBLE,
            24,652,690,100,window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)),instance_,nullptr);

        CreateWindowW(
            L"STATIC",
            L"开发测试入口：luma_studio.exe --account\\n"
            L"安全能力：邮箱验证、修改/重置密码、MFA 恢复码、登录设备管理、"
            L"会话撤销、登录失败限流及安全审计。\\n"
            L"验证/重置 Token 当前通过开发接口返回，正式环境应接入邮件/消息投递服务。",
            WS_CHILD|WS_VISIBLE,24,760,690,60,window_,nullptr,instance_,nullptr);

        EnableConnectedButtons(window_,false);
        EnableAuthenticatedButtons(window_,false);
    }

    void Connect() {
        std::uint16_t port=0;
        try {
            const auto p=std::stoul(GetText(GetDlgItem(window_,ID_PORT)));
            if(p<1||p>65535)throw std::invalid_argument("port");
            port=static_cast<std::uint16_t>(p);
        } catch(...) {
            SetStatus(window_,L"端口无效");
            return;
        }

        auto r=service_->Connect(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_HOST))),port);
        SetStatus(window_,utf8_to_wide(r.message));
        EnableConnectedButtons(window_,r.success);
        EnableAuthenticatedButtons(window_,false);
    }

    void Register() {
        auto r=service_->Register(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_USERNAME))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_EMAIL))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_DISPLAY))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_PASSWORD))));
        SetStatus(window_,utf8_to_wide(r.message));
    }

    bool RefreshProfile() {
        const auto r=service_->GetProfile();
        if(!r.success) {
            SetStatus(window_,utf8_to_wide(r.message));
            EnableAuthenticatedButtons(window_,false);
            return false;
        }

        const auto s=service_->Session();
        SetText(window_,ID_USERNAME,s.user.username);
        SetText(window_,ID_EMAIL,s.user.email);
        SetText(window_,ID_DISPLAY,s.user.display_name);
        SetText(window_,ID_AVATAR,s.user.avatar_url);

        const auto sr=service_->GetSecuritySummary();
        if(sr.success) {
            const auto security=service_->Security();
            std::wstring summary=L"资料已加载\nuser_id="+utf8_to_wide(s.user.user_id)+L"\n安全状态：邮箱=";
            summary+=security.email_verified?L"已验证":L"未验证";
            summary+=L"，MFA=";
            summary+=security.mfa_enabled?L"已启用":L"未启用";
            summary+=L"，活动会话="+std::to_wstring(security.active_session_count);
            SetStatus(window_,summary);
        } else {
            SetStatus(window_,L"资料已加载\nuser_id="+utf8_to_wide(s.user.user_id));
        }
        EnableAuthenticatedButtons(window_,true);
        return true;
    }

    void Login() {
        const auto r=service_->Login(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_USERNAME))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_PASSWORD))),
            "luma-live-pc","LumaLive PC",
            wide_to_utf8(GetText(GetDlgItem(window_,ID_MFA))));
        if(!r.success) {
            SetStatus(window_,utf8_to_wide(r.message));
            return;
        }
        RefreshProfile();
    }

    void SaveProfile() {
        auto r=service_->UpdateProfile(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_USERNAME))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_EMAIL))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_DISPLAY))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_AVATAR))));
        SetStatus(window_,utf8_to_wide(r.message));
        if(r.success)EnableAuthenticatedButtons(window_,true);
    }

    void Logout() {
        const auto r=service_->Logout();
        SetStatus(window_,utf8_to_wide(r.message));
        EnableAuthenticatedButtons(window_,false);
    }

    void DeleteAccount() {
        const int answer=MessageBoxW(
            window_,
            L"此操作会永久删除账号及其当前登录会话。\n确定继续吗？",
            L"确认删除账号",
            MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);
        if(answer!=IDYES)return;

        const auto r=service_->DeleteAccount();
        SetStatus(window_,utf8_to_wide(r.message));
        if(r.success){
            EnableAuthenticatedButtons(window_,false);
            SetWindowTextW(GetDlgItem(window_,ID_PASSWORD),L"");
        }
    }

    void RequestEmailVerification() {
        const auto r=service_->RequestEmailVerification();
        SetStatus(window_,utf8_to_wide(r.message));
    }

    void VerifyEmail() {
        const auto r=service_->VerifyEmail(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_VERIFY))));
        SetStatus(window_,utf8_to_wide(r.message));
        if(r.success)RefreshProfile();
    }

    void ChangePassword() {
        const auto r=service_->ChangePassword(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_CURRENT_PASSWORD))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_NEW_PASSWORD))));
        SetStatus(window_,utf8_to_wide(r.message));
        if(r.success)EnableAuthenticatedButtons(window_,false);
    }

    void EnableMfa() {
        const auto r=service_->EnableMfa();
        SetStatus(window_,utf8_to_wide(r.message));
        if(r.success) {
            const auto code=r.message.substr(r.message.find('=')+1);
            SetText(window_,ID_MFA,code);
        }
    }

    void DisableMfa() {
        const auto r=service_->DisableMfa(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_MFA))));
        SetStatus(window_,utf8_to_wide(r.message));
    }

    void ShowSessions() {
        const auto r=service_->GetSessions();
        if(!r.success) {
            SetStatus(window_,utf8_to_wide(r.message));
            return;
        }

        std::wstring text=L"活动登录设备：\n";
        for(const auto&s:service_->Sessions()) {
            text+=utf8_to_wide(s.device_name)+L" | "+utf8_to_wide(s.remote_address);
            text+=s.current?L" | 当前":L" | 其他";
            text+=L"\n";
        }
        SetStatus(window_,text);
    }

    void RevokeOthers() {
        const auto r=service_->RevokeOtherSessions();
        SetStatus(window_,utf8_to_wide(r.message));
    }

    void ShowEvents() {
        const auto r=service_->GetSecurityEvents();
        if(!r.success) {
            SetStatus(window_,utf8_to_wide(r.message));
            return;
        }

        std::wstring text=L"最近安全事件：\n";
        for(const auto&e:service_->SecurityEvents()) {
            text+=utf8_to_wide(e.type)+L" | "+utf8_to_wide(e.detail)+L"\n";
        }
        SetStatus(window_,text);
    }

    void RequestPasswordReset() {
        const auto r=service_->RequestPasswordReset(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_USERNAME))));
        SetStatus(window_,utf8_to_wide(r.message));
        if(r.success) {
            const auto prefix=std::string("password reset token=");
            const auto pos=r.message.find(prefix);
            if(pos!=std::string::npos)SetText(window_,ID_RESET_TOKEN,r.message.substr(pos+prefix.size()));
        }
    }

    void ResetPassword() {
        const auto r=service_->ResetPassword(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_RESET_TOKEN))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_NEW_PASSWORD))));
        SetStatus(window_,utf8_to_wide(r.message));
        if(r.success)EnableAuthenticatedButtons(window_,false);
    }

    static LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
        Window* self=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            auto* cs=reinterpret_cast<CREATESTRUCTW*>(lparam);
            self=static_cast<Window*>(cs->lpCreateParams);
            self->window_=hwnd;
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }

        if(self) {
            if(message==WM_CREATE){self->CreateControls();return 0;}
            if(message==WM_COMMAND&&HIWORD(wparam)==BN_CLICKED) {
                switch(LOWORD(wparam)){
                case ID_CONNECT:self->Connect();break;
                case ID_REGISTER:self->Register();break;
                case ID_LOGIN:self->Login();break;
                case ID_LOGOUT:self->Logout();break;
                case ID_REFRESH:self->RefreshProfile();break;
                case ID_SAVE:self->SaveProfile();break;
                case ID_DELETE:self->DeleteAccount();break;
                case ID_VERIFY_REQUEST:self->RequestEmailVerification();break;
                case ID_VERIFY_EMAIL:self->VerifyEmail();break;
                case ID_CHANGE_PASSWORD:self->ChangePassword();break;
                case ID_ENABLE_MFA:self->EnableMfa();break;
                case ID_DISABLE_MFA:self->DisableMfa();break;
                case ID_SESSIONS:self->ShowSessions();break;
                case ID_REVOKE_OTHERS:self->RevokeOthers();break;
                case ID_EVENTS:self->ShowEvents();break;
                case ID_RESET_REQUEST:self->RequestPasswordReset();break;
                case ID_RESET_PASSWORD:self->ResetPassword();break;
                default:break;
                }
                return 0;
            }
            if(message==WM_CLOSE){
                self->service_->Stop();
                DestroyWindow(hwnd);
                return 0;
            }
            if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
        }
        return DefWindowProcW(hwnd,message,wparam,lparam);
    }

    HINSTANCE instance_{};
    HWND window_{};
    std::unique_ptr<luma::client::account::IAccountService> service_;
};

}

int RunAccountView(HINSTANCE instance,int show_command){
    return Window(instance).Run(show_command);
}

}

#else

namespace luma::client::ui::account {
int RunAccountView(void*,int){return 0;}
}

#endif
