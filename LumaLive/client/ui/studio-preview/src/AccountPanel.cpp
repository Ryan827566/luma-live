#include "AccountPanel.hpp"
#include "AccountConnectionSettings.hpp"
#include "IAccountService.hpp"
#include "UiTheme.hpp"
#include "UiAccessibility.hpp"
#include "TranscriptView.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <ctime>
#include <vector>

namespace luma::client::ui::preview {
namespace {
using Service = luma::client::account::IAccountService;
using Result = luma::client::account::OperationResult;
enum class Page { Login, Register, Recovery, Profile, Security, Devices, Service };
enum class Action { None, Connect, Login, Register, RequestReset, Reset, Profile, Logout,
    Refresh, RequestEmail, VerifyEmail, RequestPhone, VerifyPhone, PhoneCode, PhoneLogin,
    Password, EnableMfa, DisableMfa, Sessions, Revoke, RevokeOthers, Events, Delete };
constexpr int TabBase=2100, FieldBase=2120, LabelBase=2140, Primary=2160, Secondary=2161,
    Third=2162, Status=2163, Detail=2164, Mode=2165, Confirm=2166, Cancel=2167, Reveal=2168;
std::wstring Wide(const std::string& text) {
    if(text.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
    std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),out.data(),n);return out;
}
std::wstring Text(HWND control) {
    const int n=GetWindowTextLengthW(control);std::wstring out(size_t(n)+1,L'\0');
    const int copied=GetWindowTextW(control,out.data(),n+1);out.resize(std::max(0,copied));return out;
}
std::string Part(const std::string& value,const std::string& key,const std::string& end=" ") {
    const auto pos=value.find(key);if(pos==std::string::npos)return {};
    const auto start=pos+key.size();return value.substr(start,value.find(end,start)-start);
}
LRESULT CALLBACK SubmitEdit(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    if(m==WM_IME_STARTCOMPOSITION)SetPropW(h,L"LumaAccountComposing",HANDLE(1));
    if(m==WM_IME_ENDCOMPOSITION)RemovePropW(h,L"LumaAccountComposing");
    if(m==WM_GETDLGCODE&&l&&reinterpret_cast<MSG*>(l)->message==WM_KEYDOWN&&reinterpret_cast<MSG*>(l)->wParam==VK_RETURN)
        return DefSubclassProc(h,m,w,l)|DLGC_WANTMESSAGE;
    if(m==WM_KEYDOWN&&w==VK_RETURN&&!GetPropW(h,L"LumaAccountComposing")){
        if(!(l&(LPARAM(1)<<30)))PostMessageW(GetParent(h),WM_COMMAND,Primary,0);
        return 0;
    }
    if(m==WM_NCDESTROY){RemovePropW(h,L"LumaAccountComposing");RemoveWindowSubclass(h,SubmitEdit,id);}
    return DefSubclassProc(h,m,w,l);
}
const wchar_t* ErrorLabel(const std::string& error) {
    if(error=="email_delivery_failed")return UiLabel(L"邮件暂时无法发送，请稍后重试或联系服务管理员。",L"The service could not send the email. Try again later or contact your administrator.");
    if(error=="sms_delivery_failed")return UiLabel(L"短信暂时无法发送，请稍后重试或联系服务管理员。",L"The service could not send the text message. Try again later or contact your administrator.");
    if(error=="invalid_challenge")return UiLabel(L"账户服务返回了无效的认证信息，已断开连接。请联系服务管理员。",L"The account service returned invalid authentication data. Disconnected; contact your administrator.");
    if(error.find("invalid_credentials")!=std::string::npos)return UiLabel(L"账户或密码不正确，请重新输入。",L"The account or password is incorrect. Try again.");
    if(error.find("locked")!=std::string::npos||error.find("rate_limit")!=std::string::npos)return UiLabel(L"尝试次数过多，请稍后再试。",L"Too many attempts. Please try again later.");
    if(error.find("mfa")!=std::string::npos||error.find("recovery code")!=std::string::npos)return UiLabel(L"请输入有效的身份验证器代码或恢复码。",L"Enter a valid authenticator or recovery code.");
    if(error.find("exists")!=std::string::npos||error.find("taken")!=std::string::npos)return UiLabel(L"用户名、邮箱或手机号已被使用。",L"That username, email or phone number is already in use.");
    if(error.find("session")!=std::string::npos||error.find("authenticated")!=std::string::npos)return UiLabel(L"登录状态已失效，请重新登录。",L"Your session is no longer valid. Sign in again.");
    if(error.find("connect")!=std::string::npos||error.find("send failed")!=std::string::npos||error.find("receive failed")!=std::string::npos||error.find("timeout")!=std::string::npos)return UiLabel(L"账户服务暂不可用，请检查服务地址后重试。",L"The account service is unavailable. Check its address and try again.");
    if(error.find("IPv4")!=std::string::npos)return UiLabel(L"请输入有效的服务器 IPv4 地址。",L"Enter a valid server IPv4 address.");
    if(error.find("token")!=std::string::npos||error.find("challenge")!=std::string::npos||error.find("code")!=std::string::npos)return UiLabel(L"验证码无效或已过期，请重新获取。",L"The verification code is invalid or expired. Request a new one.");
    return UiLabel(L"操作未完成，请检查填写的信息后重试。",L"Unable to complete the action. Check your information and try again.");
}
const wchar_t* SuccessLabel(Action action) {
    switch(action){
    case Action::Connect:return UiLabel(L"已连接账户服务。",L"Connected to the account service.");
    case Action::Login:case Action::PhoneLogin:return UiLabel(L"登录成功。",L"You are signed in.");
    case Action::Register:return UiLabel(L"账户创建成功，请使用新账户登录。",L"Your account is ready. Sign in to continue.");
    case Action::RequestReset:return UiLabel(L"如账户存在，请查收重置邮件。",L"If the account exists, check your email for a reset code.");
    case Action::RequestEmail:return UiLabel(L"验证邮件已发送，请输入邮件中的验证码。",L"Verification email sent. Enter the code from your email.");
    case Action::RequestPhone:case Action::PhoneCode:return UiLabel(L"短信已发送，请输入收到的验证码。",L"Text message sent. Enter the code you receive.");
    case Action::Logout:return UiLabel(L"已退出登录。",L"You are signed out.");
    case Action::Delete:return UiLabel(L"账户已删除。",L"Your account has been deleted.");
    case Action::Password:case Action::Reset:return UiLabel(L"密码已更新，请重新登录。",L"Password updated. Sign in again.");
    case Action::EnableMfa:return UiLabel(L"已启用双重验证。请保存右侧密钥和恢复码。",L"Two-step verification is on. Save the key and recovery codes on the right.");
    default:return UiLabel(L"已完成。",L"Done.");
    }
}
std::wstring EventText(const contracts::auth::SecurityEvent& event){
    struct Label{const char* code;const wchar_t* zh;const wchar_t* en;};
    static const Label labels[]={
        {"account_registered",L"账户已创建",L"Account created"},{"login_success",L"密码登录成功",L"Password sign-in succeeded"},
        {"login_failure",L"登录失败",L"Sign-in failed"},{"new_network",L"从新网络登录",L"Signed in from a new network"},
        {"logout",L"退出登录",L"Signed out"},{"session_refreshed",L"会话已续期",L"Session renewed"},
        {"profile_updated",L"资料已更新",L"Profile updated"},{"profile_email_changed",L"邮箱已修改",L"Email changed"},
        {"email_verification_requested",L"请求邮箱验证",L"Email verification requested"},{"email_verified",L"邮箱已验证",L"Email verified"},
        {"phone_verification_code_requested",L"请求手机验证",L"Phone verification requested"},{"phone_verified",L"手机已验证",L"Phone verified"},
        {"phone_verification_delivery_failed",L"验证短信发送失败",L"Verification message delivery failed"},
        {"phone_login_code_requested",L"请求登录短信",L"Sign-in code requested"},{"phone_login_delivery_failed",L"登录短信发送失败",L"Sign-in message delivery failed"},
        {"phone_login_success",L"短信登录成功",L"Text message sign-in succeeded"},{"phone_login_failure",L"短信登录失败",L"Text message sign-in failed"},
        {"password_changed",L"密码已修改",L"Password changed"},{"password_reset_requested",L"请求重置密码",L"Password reset requested"},
        {"password_reset_completed",L"密码已重置",L"Password reset completed"},{"password_reset_delivery_failed",L"重置邮件发送失败",L"Reset email delivery failed"},
        {"mfa_enabled",L"启用双重验证",L"Two-step verification enabled"},{"mfa_disabled",L"关闭双重验证",L"Two-step verification disabled"},
        {"session_revoked",L"移除登录设备",L"Device signed out"},{"other_sessions_revoked",L"退出其它设备",L"Other devices signed out"}
    };
    const wchar_t* label=UiLabel(L"账户安全活动",L"Account security activity");for(const auto& item:labels)if(event.type==item.code){label=UiLabel(item.zh,item.en);break;}
    const auto time=static_cast<std::time_t>(event.created_at_epoch_seconds);std::tm local{};wchar_t date[32]{};
    if(localtime_s(&local,&time)==0)std::wcsftime(date,32,L"%Y-%m-%d %H:%M",&local);
    return std::wstring(date)+L"\r\n"+label+L"\r\n\r\n";
}
struct Snapshot {
    Action action{};Result result;bool connected{},authenticated{};std::wstring settingsError;
    contracts::auth::UserProfile profile;contracts::auth::SecuritySummary security;
    std::vector<contracts::auth::DeviceSession> sessions;
    std::vector<contracts::auth::SecurityEvent> events;
};
struct Worker {
    std::unique_ptr<Service> service{luma::client::account::CreateAccountService()};
    std::mutex mutex;std::optional<Snapshot> completed;
};
}

struct AccountPanel::Impl {
    HWND window{};HFONT body{},heading{};HBRUSH background{CreateSolidBrush(UiTheme::Background)},surface{CreateSolidBrush(UiTheme::Surface)};
    std::array<HWND,4> tabs{};std::array<Page,4> pages{};
    std::array<HWND,6> fields{},labels{};std::array<bool,6> secrets{};
    HWND primary{},secondary{},third{},status{},detail{},mode{},confirm{},cancel{},reveal{};
    std::shared_ptr<Worker> worker{std::make_shared<Worker>()};
    Snapshot state;Page page{Page::Login};Action first{},second{},thirdAction{},pending{};
    std::wstring statusText;std::string host{"127.0.0.1"};std::uint16_t port{9100};
    std::string phoneChallenge,verifyChallenge,phoneNumber,verifyNumber,pendingDevice;
    bool busy{},error{},showPassword{};UINT dpi{96};
    int width{},height{},formX{},formWidth{},fieldCount{},section{};
    ~Impl(){Close();if(body)DeleteObject(body);if(heading)DeleteObject(heading);DeleteObject(background);DeleteObject(surface);}
    int S(int value)const{return MulDiv(value,int(dpi),96);}
    void Close(){if(window){DestroyWindow(window);window=nullptr;}worker.reset();}
    HWND Make(int id,const wchar_t* cls,const wchar_t* label,DWORD style=0) {
        auto h=CreateWindowExW(0,cls,label,WS_CHILD|style,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);return h;
    }
    HWND Button(int id,const wchar_t* text){auto h=Make(id,L"BUTTON",text,WS_TABSTOP|BS_OWNERDRAW);UiTheme::InstallHover(h);return h;}
    void Fonts(){
        auto oldBody=body,oldHeading=heading;body=UiTheme::Font(S(14));heading=UiTheme::Font(S(25),600);
        if(window)EnumChildWindows(window,[](HWND h,LPARAM f)->BOOL{SendMessageW(h,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},reinterpret_cast<LPARAM>(body));
        if(oldBody)DeleteObject(oldBody);if(oldHeading)DeleteObject(oldHeading);
    }
    void Open(HWND owner){
        if(window){ShowWindow(window,SW_SHOW);return;}
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpfnWndProc=Proc;wc.lpszClassName=L"LumaAccountPage";RegisterClassW(&wc);
        dpi=GetDpiForWindow(owner);Fonts();window=CreateWindowExW(WS_EX_CONTROLPARENT,L"LumaAccountPage",UiLabel(L"账户",L"Account"),WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,owner,nullptr,wc.hInstance,this);
        for(int i=0;i<4;++i)tabs[i]=Button(TabBase+i,L"");
        mode=Make(Mode,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL);UiTheme::InstallCombo(mode);
        SetAccessibleName(mode,UiLabel(L"账户操作方式",L"Account action"));
        for(int i=0;i<6;++i){labels[i]=Make(LabelBase+i,L"STATIC",L"");fields[i]=Make(FieldBase+i,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL);UiTheme::InstallEdit(fields[i]);SetWindowSubclass(fields[i],SubmitEdit,8,0);SendMessageW(fields[i],EM_SETLIMITTEXT,1024,0);}
        reveal=Button(Reveal,UiLabel(L"显示密码",L"Show password"));primary=Button(Primary,L"");secondary=Button(Secondary,L"");third=Button(Third,L"");
        detail=Make(Detail,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER);UiTheme::InstallEdit(detail);SetAccessibleName(detail,UiLabel(L"账户详情",L"Account details"));
        status=Make(Status,L"STATIC",L"",SS_LEFT);confirm=Button(Confirm,UiLabel(L"确认操作",L"Confirm"));cancel=Button(Cancel,UiLabel(L"取消",L"Cancel"));
        ShowWindow(detail,SW_SHOW);ShowWindow(status,SW_SHOW);
        SendMessageW(detail,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(S(12),S(12)));
        AccountConnectionSettings saved;std::wstring settingsError;
        const bool loaded=LoadAccountConnectionSettings(saved,settingsError);host=saved.host;port=saved.port;
        SetTimer(window,1,80,nullptr);Configure();if(!loaded)Notice(settingsError.c_str(),true);ShowWindow(window,SW_SHOW);
    }
    void Field(int i,const wchar_t* label,const std::wstring& value=L"",bool secret=false){
        ++fieldCount;secrets[i]=secret;SetWindowTextW(labels[i],label);SetAccessibleName(fields[i],label);SetWindowTextW(fields[i],value.c_str());
        SendMessageW(fields[i],EM_SETPASSWORDCHAR,secret?L'\x25cf':0,0);ShowWindow(fields[i],SW_SHOW);ShowWindow(labels[i],SW_SHOW);
    }
    void Buttons(Action a,const wchar_t* al,Action b=Action::None,const wchar_t* bl=L"",Action c=Action::None,const wchar_t* cl=L""){
        first=a;second=b;thirdAction=c;SetWindowTextW(primary,al);SetWindowTextW(secondary,bl);SetWindowTextW(third,cl);
        ShowWindow(primary,a==Action::None?SW_HIDE:SW_SHOW);ShowWindow(secondary,b==Action::None?SW_HIDE:SW_SHOW);ShowWindow(third,c==Action::None?SW_HIDE:SW_SHOW);
    }
    void Configure(){
        pending=Action::None;showPassword=false;fieldCount=0;
        SetWindowTextW(reveal,UiLabel(L"显示密码",L"Show password"));
        for(int i=0;i<6;++i){SetWindowTextW(fields[i],L"");secrets[i]=false;ShowWindow(fields[i],SW_HIDE);ShowWindow(labels[i],SW_HIDE);}
        ShowWindow(confirm,SW_HIDE);ShowWindow(cancel,SW_HIDE);ShowWindow(reveal,SW_HIDE);ShowWindow(mode,SW_HIDE);
        if(state.authenticated){pages={Page::Profile,Page::Security,Page::Devices,Page::Service};const wchar_t* names[]={UiLabel(L"个人资料",L"Profile"),UiLabel(L"账户安全",L"Security"),UiLabel(L"登录设备",L"Devices"),UiLabel(L"服务连接",L"Connection")};for(int i=0;i<4;++i)SetWindowTextW(tabs[i],names[i]);}
        else{pages={Page::Login,Page::Register,Page::Recovery,Page::Service};const wchar_t* names[]={UiLabel(L"登录",L"Sign in"),UiLabel(L"创建账户",L"Create account"),UiLabel(L"找回密码",L"Reset password"),UiLabel(L"服务连接",L"Connection")};for(int i=0;i<4;++i)SetWindowTextW(tabs[i],names[i]);}
        for(auto tab:tabs)ShowWindow(tab,SW_SHOW);
        auto setModes=[&](std::initializer_list<const wchar_t*> names){SendMessageW(mode,CB_RESETCONTENT,0,0);for(auto name:names)SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));SendMessageW(mode,CB_SETCURSEL,section,0);ShowWindow(mode,SW_SHOW);};
        switch(page){
        case Page::Login:
            setModes({UiLabel(L"密码登录",L"Password"),UiLabel(L"短信登录",L"Text message")});
            if(section==0){Field(0,UiLabel(L"用户名或邮箱",L"Username or email"));Field(1,UiLabel(L"密码",L"Password"),L"",true);Field(2,UiLabel(L"双重验证码（可选）",L"Two-step code (optional)"));Buttons(Action::Login,UiLabel(L"登录",L"Sign in"));ShowWindow(reveal,SW_SHOW);}
            else{Field(0,UiLabel(L"手机号（含国家区号）",L"Phone number with country code"));Field(1,UiLabel(L"短信验证码",L"Text message code"));Field(2,UiLabel(L"双重验证码（可选）",L"Two-step code (optional)"));Buttons(Action::PhoneLogin,UiLabel(L"登录",L"Sign in"),Action::PhoneCode,UiLabel(L"获取验证码",L"Send code"));}break;
        case Page::Register:
            Field(0,UiLabel(L"用户名（3–32 位字母、数字或 . _ -）",L"Username (3–32 letters, digits or . _ -)"));Field(1,UiLabel(L"邮箱",L"Email"));Field(2,UiLabel(L"显示名称",L"Display name"));Field(3,UiLabel(L"密码（8–128 字节）",L"Password (8–128 bytes)"),L"",true);Field(4,UiLabel(L"确认密码",L"Confirm password"),L"",true);Buttons(Action::Register,UiLabel(L"创建账户",L"Create account"));break;
        case Page::Recovery:
            Field(0,UiLabel(L"用户名或邮箱",L"Username or email"));Field(1,UiLabel(L"邮件中的重置验证码",L"Reset code from your email"));Field(2,UiLabel(L"新密码",L"New password"),L"",true);Field(3,UiLabel(L"确认新密码",L"Confirm new password"),L"",true);Buttons(Action::Reset,UiLabel(L"重置密码",L"Reset password"),Action::RequestReset,UiLabel(L"发送重置邮件",L"Send reset email"));break;
        case Page::Profile:
            Field(0,UiLabel(L"用户名",L"Username"),Wide(state.profile.username));Field(1,UiLabel(L"邮箱",L"Email"),Wide(state.profile.email));Field(2,UiLabel(L"显示名称",L"Display name"),Wide(state.profile.display_name));Field(3,UiLabel(L"头像地址（可选）",L"Avatar URL (optional)"),Wide(state.profile.avatar_url));Buttons(Action::Profile,UiLabel(L"保存资料",L"Save changes"),Action::Logout,UiLabel(L"退出登录",L"Sign out"),Action::Delete,UiLabel(L"删除账户",L"Delete account"));break;
        case Page::Security:
            setModes({UiLabel(L"邮箱验证",L"Verify email"),UiLabel(L"手机验证",L"Verify phone"),UiLabel(L"修改密码",L"Change password"),UiLabel(L"双重验证",L"Two-step verification"),UiLabel(L"安全记录",L"Security activity")});
            if(section==0){Field(0,UiLabel(L"邮件中的验证码",L"Code from your email"));Buttons(Action::VerifyEmail,UiLabel(L"验证邮箱",L"Verify email"),Action::RequestEmail,UiLabel(L"发送验证邮件",L"Send email"));}
            if(section==1){Field(0,UiLabel(L"手机号（含国家区号）",L"Phone number with country code"),Wide(state.profile.phone));Field(1,UiLabel(L"短信验证码",L"Text message code"));Buttons(Action::VerifyPhone,UiLabel(L"验证手机",L"Verify phone"),Action::RequestPhone,UiLabel(L"获取验证码",L"Send code"));}
            if(section==2){Field(0,UiLabel(L"当前密码",L"Current password"),L"",true);Field(1,UiLabel(L"新密码",L"New password"),L"",true);Field(2,UiLabel(L"确认新密码",L"Confirm new password"),L"",true);Buttons(Action::Password,UiLabel(L"更新密码",L"Update password"));}
            if(section==3){Field(0,UiLabel(L"关闭时输入恢复码",L"Recovery code to turn off"),L"",true);Buttons(state.security.mfa_enabled?Action::DisableMfa:Action::EnableMfa,state.security.mfa_enabled?UiLabel(L"关闭双重验证",L"Turn off verification"):UiLabel(L"启用双重验证",L"Turn on verification"));}break;

        case Page::Devices:
            ShowWindow(mode,SW_SHOW);SendMessageW(mode,CB_RESETCONTENT,0,0);for(const auto& device:state.sessions){auto name=Wide(device.device_name)+(device.current?UiLabel(L"（当前设备）",L" (this device)"):L"");SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}SendMessageW(mode,CB_SETCURSEL,0,0);
            Buttons(Action::Sessions,UiLabel(L"刷新设备",L"Refresh devices"),Action::Revoke,UiLabel(L"移除所选设备",L"Remove selected"),Action::RevokeOthers,UiLabel(L"退出其它设备",L"Sign out others"));break;
        case Page::Service:
            Field(0,UiLabel(L"账户服务器地址",L"Account server address"),Wide(host));Field(1,UiLabel(L"端口",L"Port"),std::to_wstring(port));Buttons(Action::Connect,UiLabel(L"连接服务",L"Connect"),state.authenticated?Action::Refresh:Action::None,UiLabel(L"刷新会话",L"Refresh session"));break;
        }
        if(page==Page::Security&&section==4)Buttons(Action::Events,UiLabel(L"刷新安全记录",L"Refresh activity"));
        ShowWindow(reveal,std::any_of(secrets.begin(),secrets.end(),[](bool value){return value;})?SW_SHOW:SW_HIDE);
        RefreshDetail();Layout();Enable();
    }
    void RefreshDetail(){
        std::wstring text;
        if(state.authenticated){text=Wide(state.profile.display_name)+L"\r\n"+Wide(state.profile.email)+L"\r\n\r\n";
            text+=state.security.email_verified?UiLabel(L"邮箱已验证",L"Email verified"):UiLabel(L"邮箱未验证",L"Email not verified");text+=L"\r\n";
            text+=state.security.phone_verified?UiLabel(L"手机已验证",L"Phone verified"):UiLabel(L"手机未验证",L"Phone not verified");text+=L"\r\n";
            text+=state.security.mfa_enabled?UiLabel(L"双重验证已启用",L"Two-step verification on"):UiLabel(L"双重验证未启用",L"Two-step verification off");
            if(page==Page::Devices){text+=L"\r\n\r\n";for(const auto& device:state.sessions)text+=Wide(device.device_name)+L"\r\n"+Wide(device.remote_address)+L"\r\n\r\n";}
        }else text=UiLabel(L"管理身份与登录设备。切换页面时，通话和会议会继续运行。",L"Manage your identity and signed-in devices. Calls and meetings keep running when you switch pages.");
        if(page==Page::Service)text=UiLabel(L"输入管理员提供的 IPv4 地址和端口。连接成功后保存，下次自动填入。",L"Enter your administrator's server IPv4 address and port. A successful connection saves them for your next visit.");
        if(page==Page::Security&&section==4){text=UiLabel(L"安全记录（本地时间）\r\n\r\n",L"Security activity (local time)\r\n\r\n");for(const auto& event:state.events)text+=EventText(event);if(state.events.empty())text+=UiLabel(L"暂无记录。",L"No activity yet.");}
        SetWindowTextW(detail,text.c_str());UpdateTranscriptScrollbar(detail);
    }
    void Move(HWND h,int x,int y,int w,int heightValue){MoveWindow(h,S(x),S(y),S(std::max(1,w)),S(heightValue),TRUE);}
    void Layout(){
        if(!window)return;RECT r{};GetClientRect(window,&r);width=MulDiv(r.right,96,int(dpi));height=MulDiv(r.bottom,96,int(dpi));
        if(!state.authenticated){
            // Keep the longest form, its result and its guidance visible at 621 DIP.
            // Native controls stay alive so selection, IME and pending requests survive.
            const bool compact=height<700;const int offset=std::clamp((height-700)/3,0,48);
            formWidth=std::min(440,std::max(1,width-64));formX=(width-formWidth)/2;
            const int tabsWidth=std::min(608,std::max(1,width-56)),tabWidth=(tabsWidth-24)/4;
            const int tabsX=(width-(tabWidth*4+24))/2;
            for(int i=0;i<4;++i)Move(tabs[i],tabsX+i*(tabWidth+8),96+offset,tabWidth,36);
            int y=(compact?148:172)+offset;
            if(GetWindowLongPtrW(mode,GWL_STYLE)&WS_VISIBLE){Move(mode,formX,y,formWidth,180);UiTheme::SetComboDpi(mode,dpi);y+=44;}
            int revealField=-1;for(int i=0;i<fieldCount;++i)if(secrets[i])revealField=i;
            for(int i=0;i<fieldCount;++i){
                Move(labels[i],formX,y,formWidth-(i==revealField?142:0),18);
                if(i==revealField)Move(reveal,formX+formWidth-134,y-2,134,24);
                Move(fields[i],formX,y+22,formWidth,34);y+=compact?60:68;
            }
            const int actionY=y+8,half=(formWidth-8)/2;
            Move(primary,formX,actionY,second==Action::None?formWidth:half,36);
            Move(secondary,formX+half+8,actionY,half,36);
            Move(third,formX,actionY+44,formWidth,30);
            const int statusY=actionY+48;
            Move(status,formX,statusY,formWidth,52);
            SetPropW(detail,L"LumaQuietField",HANDLE(1));
            Move(detail,formX,statusY+60,formWidth,48);UpdateTranscriptScrollbar(detail);
            Move(confirm,formX,statusY+116,half,34);Move(cancel,formX+half+8,statusY+116,half,34);
            InvalidateRect(window,nullptr,TRUE);return;
        }
        RemovePropW(detail,L"LumaQuietField");
        formX=28;const int content=width-56;formWidth=std::min(440,(content*57)/100);
        const int tabWidth=std::min(152,(content-24)/4);for(int i=0;i<4;++i)Move(tabs[i],formX+i*(tabWidth+8),96,tabWidth,36);
        int y=156;if(GetWindowLongPtrW(mode,GWL_STYLE)&WS_VISIBLE){Move(mode,formX,y,formWidth,180);UiTheme::SetComboDpi(mode,dpi);y+=52;}
        int revealField=-1;for(int i=0;i<fieldCount;++i)if(secrets[i])revealField=i;
        for(int i=0;i<fieldCount;++i){
            Move(labels[i],formX,y,formWidth-(i==revealField?142:0),20);
            if(i==revealField)Move(reveal,formX+formWidth-134,y-2,134,24);
            Move(fields[i],formX,y+24,formWidth,34);y+=68;
        }
        const int actionY=y+12,half=(formWidth-8)/2;
        Move(primary,formX,actionY,second==Action::None?formWidth:half,36);Move(secondary,formX+half+8,actionY,half,36);
        Move(third,formX,actionY+44,formWidth,30);
        const int detailX=formX+formWidth+40;Move(detail,detailX,156,width-detailX-28,std::max(160,height-316));UpdateTranscriptScrollbar(detail);
        Move(status,detailX,height-144,width-detailX-28,72);Move(confirm,detailX,height-60,(width-detailX-36)/2,34);Move(cancel,detailX+(width-detailX-36)/2+8,height-60,(width-detailX-36)/2,34);
        InvalidateRect(window,nullptr,TRUE);
    }
    void Enable(){
        for(auto f:fields)EnableWindow(f,!busy);for(auto t:tabs)EnableWindow(t,!busy);
        for(auto h:{primary,secondary,third,mode,reveal,confirm,cancel})EnableWindow(h,!busy);
        if(page==Page::Devices&&state.sessions.empty())EnableWindow(secondary,FALSE);
        SetWindowTextW(status,statusText.c_str());InvalidateRect(status,nullptr,TRUE);
    }
    void Notice(const wchar_t* text,bool failure=false){statusText=text;error=failure;SetWindowTextW(status,statusText.c_str());InvalidateRect(status,nullptr,TRUE);}
    bool CancelPending(){if(pending==Action::None)return false;pending=Action::None;pendingDevice.clear();ShowWindow(confirm,SW_HIDE);ShowWindow(cancel,SW_HIDE);Notice(UiLabel(L"已取消。",L"Cancelled."));Enable();SetFocus(thirdAction==Action::Delete?third:secondary);return true;}
    bool Required(int i){if(Text(fields[i]).empty()){Notice(UiLabel(L"请填写必填信息。",L"Complete the required fields."),true);SetFocus(fields[i]);return false;}return true;}
    void Start(Action action,bool confirmed=false){
        if(busy||action==Action::None)return;
        if((action==Action::Delete||action==Action::Revoke||action==Action::RevokeOthers||action==Action::DisableMfa)&&!confirmed){
            std::wstring prompt=action==Action::Delete?UiLabel(L"删除账户后无法恢复，确认继续？",L"Deleting your account cannot be undone. Continue?"):UiLabel(L"确认执行此安全操作？",L"Confirm this security action?");
            if(action==Action::Revoke){const auto index=SendMessageW(mode,CB_GETCURSEL,0,0);if(index<0||size_t(index)>=state.sessions.size())return;pendingDevice=state.sessions[index].session_id;prompt=UiLabel(L"确认退出设备：",L"Sign out device: ")+Wide(state.sessions[index].device_name)+L"?";}
            pending=action;Notice(prompt.c_str());EnableWindow(mode,FALSE);ShowWindow(confirm,SW_SHOW);ShowWindow(cancel,SW_SHOW);SetFocus(cancel);return;
        }
        pending=Action::None;ShowWindow(confirm,SW_HIDE);ShowWindow(cancel,SW_HIDE);Enable();
        std::array<std::string,6> values;for(int i=0;i<6;++i)values[i]=UiUtf8(Text(fields[i]).c_str());
        auto required=[&](std::initializer_list<int> indices){for(int i:indices)if(!Required(i))return false;return true;};
        if((action==Action::Login&&!required({0,1}))||(action==Action::Register&&!required({0,1,2,3,4}))||(action==Action::Reset&&!required({1,2,3}))||(action==Action::Password&&!required({0,1,2}))||((action==Action::RequestReset||action==Action::RequestPhone||action==Action::PhoneCode||action==Action::VerifyEmail||action==Action::DisableMfa)&&!required({0})))return;
        if((action==Action::Register&&values[3]!=values[4])||(action==Action::Reset&&values[2]!=values[3])||(action==Action::Password&&values[1]!=values[2])){Notice(UiLabel(L"两次输入的密码不一致。",L"The passwords do not match."),true);return;}
        if(action==Action::Connect){
            if(!required({0,1}))return;
            if(values[1].find_first_not_of("0123456789")!=std::string::npos||values[1].size()>5){Notice(UiLabel(L"端口必须在 1–65535 之间。",L"Port must be between 1 and 65535."),true);return;}
            const auto number=std::stoul(values[1]);if(!number||number>65535){Notice(UiLabel(L"端口必须在 1–65535 之间。",L"Port must be between 1 and 65535."),true);return;}host=values[0];port=static_cast<std::uint16_t>(number);
        }
        auto challenge=action==Action::PhoneLogin?phoneChallenge:verifyChallenge;
        const auto challengeNumber=action==Action::PhoneLogin?phoneNumber:verifyNumber;
        if((action==Action::PhoneLogin||action==Action::VerifyPhone)&&(challenge.empty()||values[0]!=challengeNumber||!required({0,1}))){Notice(UiLabel(L"请为当前手机号重新获取验证码，再输入收到的代码。",L"Request a code for this phone number, then enter the code you receive."),true);return;}
        const std::string device=action==Action::Revoke?pendingDevice:std::string{};pendingDevice.clear();
        if(action==Action::PhoneCode){phoneNumber=values[0];phoneChallenge.clear();}if(action==Action::RequestPhone){verifyNumber=values[0];verifyChallenge.clear();}
        busy=true;Notice(UiLabel(L"正在处理，请稍候…",L"Working. Please wait…"));Enable();
        for(int i=0;i<6;++i)if(secrets[i])SetWindowTextW(fields[i],L"");
        auto shared=worker;const auto address=host;const auto servicePort=port;
        try{std::thread([shared,action,address,servicePort,values=std::move(values),challenge,device]()mutable{
            Snapshot out;out.action=action;auto& s=*shared->service;
            try{
                Result result{true,{}};
                if(action!=Action::Logout&&(action==Action::Connect||!s.IsRunning()))result=s.Connect(address,servicePort);
                // Renew a naturally expired access token before sending the user's operation.
                if(result.success&&action!=Action::Logout&&action!=Action::Refresh&&s.IsRunning()&&!s.IsAuthenticated()&&!s.Session().refresh_token.empty())result=s.RefreshSession();
                if(result.success){switch(action){
                case Action::Connect:break;
                case Action::Login:result=s.Login(values[0],values[1],"luma-studio","LumaLive Studio",values[2]);break;
                case Action::Register:result=s.Register(values[0],values[1],values[2],values[3]);break;
                case Action::RequestReset:result=s.RequestPasswordReset(values[0]);break;
                case Action::Reset:result=s.ResetPassword(values[1],values[2]);break;
                case Action::Profile:result=s.UpdateProfile(values[0],values[1],values[2],values[3]);break;
                case Action::Logout:result=s.Logout();s.Stop();result={true,{}};break;
                case Action::Refresh:result=s.RefreshSession();break;
                case Action::RequestEmail:result=s.RequestEmailVerification();break;
                case Action::VerifyEmail:result=s.VerifyEmail(values[0]);break;
                case Action::RequestPhone:result=s.RequestPhoneVerification(values[0]);break;
                case Action::VerifyPhone:result=s.VerifyPhone(challenge,values[1]);break;
                case Action::PhoneCode:result=s.RequestPhoneLoginCode(values[0]);break;
                case Action::PhoneLogin:result=s.LoginWithPhoneCode(challenge,values[1],"luma-studio","LumaLive Studio",values[2]);break;
                case Action::Password:result=s.ChangePassword(values[0],values[1]);if(result.success)s.Stop();break;
                case Action::EnableMfa:result=s.EnableMfa();break;
                case Action::DisableMfa:result=s.DisableMfa(values[0]);break;
                case Action::Sessions:result=s.GetSessions();break;
                case Action::Revoke:result=s.RevokeSession(device);if(result.success&&s.IsAuthenticated())s.GetSessions();break;
                case Action::RevokeOthers:result=s.RevokeOtherSessions();if(result.success)s.GetSessions();break;
                case Action::Events:result=s.GetSecurityEvents();break;
                case Action::Delete:result=s.DeleteAccount();break;
                default:break;
                }}
                if(result.success&&action==Action::Connect)SaveAccountConnectionSettings({address,servicePort},out.settingsError);
                out.result=std::move(result);
                if(out.result.success&&s.IsAuthenticated()&&(action==Action::Login||action==Action::PhoneLogin||action==Action::Profile||action==Action::Refresh||action==Action::VerifyEmail||action==Action::VerifyPhone)){
                    auto checked=s.GetSecuritySummary();if(checked.success)checked=s.GetProfile();if(!checked.success)out.result=std::move(checked);
                }
                out.authenticated=s.IsAuthenticated();out.connected=s.IsRunning();out.profile=s.Session().user;out.security=s.Security();out.sessions=s.Sessions();out.events=s.SecurityEvents();
            }catch(...){s.Stop();out.result={false,"request failed"};}
            for(auto& value:values)if(!value.empty())SecureZeroMemory(value.data(),value.size());
            std::lock_guard lock(shared->mutex);shared->completed=std::move(out);
        }).detach();}catch(...){busy=false;Notice(UiLabel(L"暂时无法启动请求，请重试。",L"Unable to start the request. Try again."),true);Enable();}
    }
    void Poll(){
        std::optional<Snapshot> completed;{std::lock_guard lock(worker->mutex);completed.swap(worker->completed);}if(!completed)return;
        busy=false;const bool wasAuthenticated=state.authenticated;state=std::move(*completed);const auto action=state.action;auto message=std::move(state.result.message);const bool success=state.result.success;
        if(success&&state.authenticated&&(action==Action::Login||action==Action::PhoneLogin)){page=Page::Profile;section=0;Configure();if(IsWindowVisible(window))SetFocus(fields[0]);}
        else if((success&&(action==Action::Register||action==Action::Reset||action==Action::Password||action==Action::Logout||action==Action::Delete))||(wasAuthenticated&&!state.authenticated)){page=Page::Login;section=0;phoneChallenge.clear();verifyChallenge.clear();Configure();if(IsWindowVisible(window))SetFocus(fields[0]);}
        else if(success&&(action==Action::Profile||action==Action::Connect||action==Action::Refresh||action==Action::DisableMfa||action==Action::VerifyEmail||action==Action::VerifyPhone||action==Action::Revoke||action==Action::RevokeOthers||action==Action::Sessions)){if(page==Page::Devices||action==Action::DisableMfa)Configure();else RefreshDetail();}
        if(success){
            if(action==Action::Connect&&!state.settingsError.empty())Notice((std::wstring(SuccessLabel(action))+L" "+state.settingsError).c_str(),true);
            else Notice(SuccessLabel(action));
            if(action==Action::Events)RefreshDetail();
            if(action==Action::PhoneCode)phoneChallenge=Part(message,"challenge="," expires=");
            if(action==Action::RequestPhone)verifyChallenge=Part(message,"challenge="," expires=");
            if(action==Action::EnableMfa){Configure();std::wstring text=UiLabel(L"身份验证器密钥\r\n",L"Authenticator key\r\n")+Wide(Part(message,"TOTP secret=","; otpauth="))+L"\r\n\r\n"+UiLabel(L"恢复码（妥善保存）\r\n",L"Recovery codes (keep safe)\r\n")+Wide(Part(message,"recovery codes=","; TOTP secret="));SetWindowTextW(detail,text.c_str());UpdateTranscriptScrollbar(detail);if(IsWindowVisible(window))SetFocus(detail);}
        }else Notice(ErrorLabel(state.result.code.empty()?message:state.result.code),true);
        if(!message.empty())SecureZeroMemory(message.data(),message.size());Enable();InvalidateRect(window,nullptr,FALSE);
    }
    void Paint(HDC dc){
        RECT r;GetClientRect(window,&r);UiTheme::Fill(dc,r,UiTheme::Background);const int saved=SaveDC(dc);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,UiTheme::Text);SelectObject(dc,heading);
        const int offset=state.authenticated?0:std::clamp((height-700)/3,0,48);
        const UINT alignment=state.authenticated?DT_LEFT:DT_CENTER;
        const wchar_t* titleText=UiLabel(L"账户与安全",L"Account & security");
        const wchar_t* subtitle=UiLabel(L"管理个人资料与登录方式",L"Manage your profile and sign-in methods");
        if(!state.authenticated){
            titleText=UiLabel(L"登录 LumaLive",L"Sign in to LumaLive");subtitle=UiLabel(L"连接你的工作空间",L"Connect to your workspace");
            if(page==Page::Register){titleText=UiLabel(L"创建你的账户",L"Create your account");subtitle=UiLabel(L"开启你的 LumaLive 工作空间",L"Get started with your LumaLive workspace");}
            if(page==Page::Recovery){titleText=UiLabel(L"找回密码",L"Reset your password");subtitle=UiLabel(L"通过邮箱验证，重新访问账户",L"Verify your email to regain access");}
            if(page==Page::Service){titleText=UiLabel(L"连接账户服务",L"Connect your account service");subtitle=UiLabel(L"使用管理员提供的连接信息",L"Use the connection details from your administrator");}
        }
        RECT title{S(28),S(22+offset),r.right-S(28),S(56+offset)};DrawTextW(dc,titleText,-1,&title,alignment|DT_SINGLELINE|DT_VCENTER);SelectObject(dc,body);SetTextColor(dc,UiTheme::Muted);
        RECT sub{S(28),S(60+offset),r.right-S(28),S(82+offset)};DrawTextW(dc,subtitle,-1,&sub,alignment|DT_SINGLELINE|DT_END_ELLIPSIS);
        RestoreDC(dc,saved);
    }
    LRESULT Handle(UINT m,WPARAM w,LPARAM l){
        switch(m){
        case WM_SIZE:Layout();return 0;
        case WM_TIMER:Poll();return 0;
        case WM_COMMAND:{const int id=LOWORD(w);if(id>=TabBase&&id<TabBase+4&&HIWORD(w)==BN_CLICKED&&!busy){page=pages[id-TabBase];section=0;Notice(L"");Configure();if(page==Page::Devices)Start(Action::Sessions);return 0;}
            if(id==Mode&&HIWORD(w)==CBN_SELCHANGE&&page!=Page::Devices&&!busy){section=int(SendMessageW(mode,CB_GETCURSEL,0,0));Notice(L"");Configure();if(page==Page::Security&&section==4)Start(Action::Events);return 0;}
            if(HIWORD(w)!=BN_CLICKED)return 0;
            if(id==Primary)Start(first);if(id==Secondary)Start(second);if(id==Third)Start(thirdAction);if(id==Confirm)Start(pending,true);if(id==Cancel)CancelPending();
            if(id==Reveal){showPassword=!showPassword;for(int i=0;i<6;++i)if(secrets[i]){SendMessageW(fields[i],EM_SETPASSWORDCHAR,showPassword?0:L'\x25cf',0);InvalidateRect(fields[i],nullptr,TRUE);}SetWindowTextW(reveal,showPassword?UiLabel(L"隐藏密码",L"Hide password"):UiLabel(L"显示密码",L"Show password"));}return 0;}
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(w);const auto control=reinterpret_cast<HWND>(l);const bool quiet=control==detail&&!state.authenticated;const bool edit=!quiet&&(m!=WM_CTLCOLORSTATIC||control==detail);SetTextColor(dc,control==status&&error?UiTheme::Danger:(quiet?UiTheme::Muted:UiTheme::Text));SetBkColor(dc,edit?UiTheme::Surface:UiTheme::Background);return reinterpret_cast<LRESULT>(edit?surface:background);}
        case WM_MEASUREITEM:if(auto d=reinterpret_cast<MEASUREITEMSTRUCT*>(l);d->CtlType==ODT_COMBOBOX){d->itemHeight=S(d->itemID==UINT(-1)?34:30);return TRUE;}break;
        case WM_DRAWITEM:{auto& d=*reinterpret_cast<DRAWITEMSTRUCT*>(l);if(d.CtlType==ODT_COMBOBOX)UiTheme::DrawComboItem(d,body);else UiTheme::DrawButton(d,body,d.CtlID==Primary||d.CtlID==Confirm,d.CtlID>=TabBase&&d.CtlID<TabBase+4&&pages[d.CtlID-TabBase]==page,d.CtlID==Third&&thirdAction==Action::Delete);return TRUE;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(window,&ps);Paint(dc);EndPaint(window,&ps);return 0;}
        case WM_PRINTCLIENT:Paint(reinterpret_cast<HDC>(w));return 0;
        case WM_NCDESTROY:KillTimer(window,1);break;
        }return DefWindowProcW(window,m,w,l);
    }
    static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}return self?self->Handle(m,w,l):DefWindowProcW(h,m,w,l);}
};
AccountPanel::AccountPanel():impl_(std::make_unique<Impl>()){}
AccountPanel::~AccountPanel()=default;
void AccountPanel::Open(HWND owner){impl_->Open(owner);}
void AccountPanel::Hide(){if(impl_->window)ShowWindow(impl_->window,SW_HIDE);}
void AccountPanel::Place(RECT bounds,UINT dpi){if(impl_->dpi!=dpi){impl_->dpi=dpi;impl_->Fonts();}MoveWindow(impl_->window,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,TRUE);impl_->Layout();}
bool AccountPanel::CancelPendingAction(){return impl_->CancelPending();}
void AccountPanel::Close(){impl_->Close();}
}
