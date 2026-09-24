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
constexpr int ID_HOST=100,ID_PORT=101,ID_USERNAME=102,ID_EMAIL=103,ID_DISPLAY=104,ID_PASSWORD=105,ID_AVATAR=106;
constexpr int ID_CONNECT=200,ID_REGISTER=201,ID_LOGIN=202,ID_LOGOUT=203,ID_REFRESH=204,ID_SAVE=205,ID_DELETE=206,ID_STATUS=300;

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

void EnableActionButtons(HWND window,bool connected) {
    EnableWindow(GetDlgItem(window,ID_REGISTER),connected);
    EnableWindow(GetDlgItem(window,ID_LOGIN),connected);
    EnableWindow(GetDlgItem(window,ID_LOGOUT),false);
    EnableWindow(GetDlgItem(window,ID_REFRESH),false);
    EnableWindow(GetDlgItem(window,ID_SAVE),false);
    EnableWindow(GetDlgItem(window,ID_DELETE),false);
}

void EnableAuthenticatedButtons(HWND window,bool authenticated) {
    EnableWindow(GetDlgItem(window,ID_LOGOUT),authenticated);
    EnableWindow(GetDlgItem(window,ID_REFRESH),authenticated);
    EnableWindow(GetDlgItem(window,ID_SAVE),authenticated);
    EnableWindow(GetDlgItem(window,ID_DELETE),authenticated);
}

class Window {
public:
    explicit Window(HINSTANCE instance):instance_(instance),service_(luma::client::account::CreateAccountService()){}

    int Run(int show) {
        WNDCLASSW wc{};
        wc.lpfnWndProc=&Window::Proc;
        wc.hInstance=instance_;
        wc.lpszClassName=L"LumaLiveAccountWindow";
        wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        RegisterClassW(&wc);

        window_=CreateWindowExW(
            0,wc.lpszClassName,L"LumaLive 账号中心",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,620,560,nullptr,nullptr,instance_,this);
        if(!window_) return 1;

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
        AddEdit(ID_HOST,L"127.0.0.1",130,16,300);
        AddLabel(L"端口",450,20,40);
        AddEdit(ID_PORT,L"9100",500,16,70);

        AddLabel(L"用户名 / 邮箱",24,62,100);
        AddEdit(ID_USERNAME,L"",130,58,440);

        AddLabel(L"邮箱",24,104,100);
        AddEdit(ID_EMAIL,L"",130,100,440);

        AddLabel(L"显示名称",24,146,100);
        AddEdit(ID_DISPLAY,L"",130,142,440);

        AddLabel(L"密码",24,188,100);
        AddEdit(ID_PASSWORD,L"",130,184,440,true);

        AddLabel(L"头像 URL",24,230,100);
        AddEdit(ID_AVATAR,L"",130,226,440);

        AddButton(ID_CONNECT,L"连接认证服务器",24,272,150);
        AddButton(ID_REGISTER,L"注册账号",184,272,100);
        AddButton(ID_LOGIN,L"登录",294,272,100);
        AddButton(ID_LOGOUT,L"退出登录",404,272,100);

        AddButton(ID_REFRESH,L"刷新资料",24,316,120);
        AddButton(ID_SAVE,L"保存资料",154,316,120);
        AddButton(ID_DELETE,L"删除账号",284,316,120);

        CreateWindowW(
            L"STATIC",L"未连接",WS_CHILD|WS_VISIBLE,
            24,366,546,82,window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)),instance_,nullptr);

        CreateWindowW(
            L"STATIC",
            L"开发测试入口：luma_studio.exe --account\n"
            L"账号基础能力：资料查询、用户名/邮箱/显示名称/头像 URL 修改、账号删除。",
            WS_CHILD|WS_VISIBLE,24,462,546,48,window_,nullptr,instance_,nullptr);

        EnableActionButtons(window_,false);
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
        EnableActionButtons(window_,r.success);
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
        SetStatus(
            window_,
            L"资料已加载\nuser_id="+utf8_to_wide(s.user.user_id));
        EnableAuthenticatedButtons(window_,true);
        return true;
    }

    void Login() {
        auto r=service_->Login(
            wide_to_utf8(GetText(GetDlgItem(window_,ID_USERNAME))),
            wide_to_utf8(GetText(GetDlgItem(window_,ID_PASSWORD))));
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
        if(r.success)EnableAuthenticatedButtons(window_,false);
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
        if(r.success) {
            EnableAuthenticatedButtons(window_,false);
            SetWindowTextW(GetDlgItem(window_,ID_PASSWORD),L"");
        }
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
            if(message==WM_CREATE) {
                self->CreateControls();
                return 0;
            }

            if(message==WM_COMMAND && HIWORD(wparam)==BN_CLICKED) {
                switch(LOWORD(wparam)) {
                case ID_CONNECT:self->Connect();break;
                case ID_REGISTER:self->Register();break;
                case ID_LOGIN:self->Login();break;
                case ID_LOGOUT:self->Logout();break;
                case ID_REFRESH:self->RefreshProfile();break;
                case ID_SAVE:self->SaveProfile();break;
                case ID_DELETE:self->DeleteAccount();break;
                default:break;
                }
                return 0;
            }

            if(message==WM_CLOSE) {
                self->service_->Stop();
                DestroyWindow(hwnd);
                return 0;
            }

            if(message==WM_DESTROY) {
                PostQuitMessage(0);
                return 0;
            }
        }

        return DefWindowProcW(hwnd,wparam==0?message:message,wparam,lparam);
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
