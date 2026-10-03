#include "UiTheme.hpp"
#include "CaptionOverlay.hpp"
#include "StudioPreview.hpp"
#include "PreviewMedia.hpp"
#include "AudioOutput.hpp"
#include "PreviewCall.hpp"
#include "AiPanel.hpp"
#include "DeviceCaptureFactory.hpp"
#include "ScreenCaptureService.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mfplay.h>
#include <mfapi.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <cwctype>
#include <vector>
#include <string>
#include <thread>

namespace luma::client::ui::preview {
namespace {
using Microsoft::WRL::ComPtr;
constexpr COLORREF Bg=RGB(8,10,13), Panel=RGB(16,19,24), Border=RGB(39,45,55), Ink=RGB(232,237,245), Muted=RGB(145,155,171), Mint=RGB(0,145,245);
enum Id {Open=100,Camera,Microphone,Monitor,Volume,TestSound,Refresh,CameraList,MicList,Pause,Stop,FileMode,CameraMode,FullScreen,Join,Host,Room,RemoteMode,Identity,Peers,Dial,AcceptCall,RejectCall,EndCall,ShareScreen,Reconnect,Meeting,Ai,Advanced};
std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
std::wstring CallMessage(const std::string& value){
 if(!ChineseUi())return Wide(value);
 const std::pair<const char*,const wchar_t*> messages[]={
 {"Connected to ",L"已连接："},{"Connecting to ",L"正在连接："},{"Calling ",L"正在呼叫："},{"Incoming call from ",L"收到来电："},
 {"Joining room",L"正在加入房间"},{"Ready; choose a participant to call",L"已就绪，请选择通话对象"},{"Requesting connection recovery",L"正在请求恢复连接"},{"Reconnecting media",L"正在重新连接音视频"},
 {"Connection interrupted; attempting recovery",L"连接中断，正在尝试恢复"},{"Connection failed; reconnect or end the call",L"连接失败，请重新连接或结束通话"},{"Signaling disconnected; rejoin to reconnect",L"服务器连接断开，请重新加入房间"},
 {"Room registration timed out",L"加入房间超时"},{"Call timed out",L"呼叫超时"},{"Call ended",L"通话已结束"},{"Call rejected",L"对方拒绝了通话"},{"Call cancelled",L"呼叫已取消"},{"Offline",L"尚未连接"}};
 for(const auto& item:messages)if(value.rfind(item.first,0)==0)return std::wstring(UiLabel(item.second,item.second))+Wide(value.substr(std::char_traits<char>::length(item.first)));
 return UiLabel(L"连接未完成，请检查房间状态或重新加入",L"Check the room status or rejoin");
}
void Fill(HDC dc,RECT r,COLORREF c){auto b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
void Text(HDC dc,const std::wstring& s,RECT r,HFONT font,COLORREF color=Ink,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,s.c_str(),-1,&r,flags);SelectObject(dc,old);}
std::wstring Hr(HRESULT hr){wchar_t b[24];swprintf_s(b,L"0x%08X",static_cast<unsigned>(hr));return b;}

struct PlaybackState {std::atomic<bool> closing{false},ready{false},ended{false};std::atomic<HRESULT> error{S_OK};};
class PlayerEvents final:public IMFPMediaPlayerCallback {
    std::atomic<ULONG> refs_{1};std::shared_ptr<PlaybackState> state_;
public:
    explicit PlayerEvents(std::shared_ptr<PlaybackState> s):state_(std::move(s)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IMFPMediaPlayerCallback)){*out=static_cast<IMFPMediaPlayerCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs_;if(!n)delete this;return n;}
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override {
        if(state_->closing)return;
        if(FAILED(event->hrEvent)){state_->error=event->hrEvent;return;}
        if(event->eEventType==MFP_EVENT_TYPE_MEDIAITEM_CREATED){auto* created=MFP_GET_MEDIAITEM_CREATED_EVENT(event);state_->error=event->pMediaPlayer->SetMediaItem(created->pMediaItem);}
        if(event->eEventType==MFP_EVENT_TYPE_MEDIAITEM_SET){state_->error=event->pMediaPlayer->Play();state_->ready=true;}
        if(event->eEventType==MFP_EVENT_TYPE_PLAYBACK_ENDED)state_->ended=true;
    }
};

class StudioWindow {
    HWND window_{},surface_{},remoteSurface_{};HINSTANCE instance_{};
    HFONT body_{},small_{},title_{},brand_{};HBRUSH panelBrush_{CreateSolidBrush(Panel)};
    std::array<HWND,33> controls_{};
    std::unique_ptr<media::IDeviceCaptureService> capture_;
    media::CaptureDeviceList cameras_,microphones_;
    media::ScreenCaptureService screen_;
    VideoMailbox video_;AudioOutput audio_;
    VideoMailbox remoteVideo_;AudioOutput remoteAudio_;PreviewCall call_;AiPanel ai_;std::atomic<bool> aiCallActive_{false};
    std::vector<std::string> shownPeers_;
    CallState shownCallState_{CallState::Offline};
    std::string reportedScreenError_;
    bool expectCamera_{false},expectMicrophone_{false};
    std::atomic<uint64_t> remoteVideos_{0},remoteAudios_{0};
    std::atomic<float> remotePeak_{0};
    std::atomic<float> peak_{0};std::atomic<bool> monitor_{false};std::atomic<uint64_t> videoCount_{0},audioCount_{0};
    std::atomic<bool> audioFailed_{false};
    std::shared_ptr<PlaybackState> playback_;
    ComPtr<IMFPMediaPlayer> player_;
    bool connectionSettings_{false};
    bool aiView_{false},meetingView_{false},meetingAiContext_{false};
    std::unique_ptr<HostedMeeting> meetingPage_;
    bool cameraView_{true},paused_{false},fullscreen_{false},closing_{false};
    RECT preview_{},remotePreview_{},savedWindow_{};DWORD savedStyle_{};
    std::wstring status_{UiLabel(L"选择摄像头，或打开一个视频 / 音频文件",L"Choose a camera or open a media file")},fileName_{UiLabel(L"尚未打开媒体",L"No media selected")};
    int width_{1440},height_{900},left_{176},right_{280},lower_{420},volume_{65};
    UINT dpi_{96};
    HWND Control(int id)const{return controls_[id-100];}
    int S(int n)const{return MulDiv(n,dpi_,96);}
    void Place(int id,int x,int y,int w,int h){MoveWindow(Control(id),S(x),S(y),S(w),S(h),TRUE);}
    HWND Make(int id,const wchar_t* cls,const wchar_t* label,DWORD style){auto h=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,1,1,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);controls_[id-100]=h;SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(body_),TRUE);return h;}
    void Button(int id,const wchar_t* label){UiTheme::InstallHover(Make(id,L"BUTTON",label,BS_OWNERDRAW));}
    void Fonts(){for(auto f:{body_,small_,title_,brand_})if(f)DeleteObject(f);auto make=[&](int size,int weight){return CreateFontW(-S(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,ChineseUi()?L"Microsoft YaHei UI":L"Segoe UI");};body_=make(14,400);small_=make(12,400);title_=make(22,600);brand_=make(23,700);for(auto c:controls_)if(c)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(body_),TRUE);}
    RECT R(int x,int y,int w,int h)const{return {S(x),S(y),S(x+w),S(y+h)};}
    void Label(HDC dc,const std::wstring& s,int x,int y,int w,int h,HFONT f,COLORREF c=Ink,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){Text(dc,s,R(x,y,w,h),f,c,flags);}
    void CloseFile(){if(playback_)playback_->closing=true;if(player_){player_->Shutdown();player_.Reset();}playback_.reset();paused_=false;}
    void Enumerate(){
        cameras_=capture_->EnumerateDevices(media::CaptureDeviceType::Camera);microphones_=capture_->EnumerateDevices(media::CaptureDeviceType::Microphone);
        auto fill=[&](int id,const auto& list,const wchar_t* empty){SendMessageW(Control(id),CB_RESETCONTENT,0,0);for(auto& d:list.devices)SendMessageW(Control(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Wide(d.name).c_str()));if(list.devices.empty())SendMessageW(Control(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(empty));SendMessageW(Control(id),CB_SETCURSEL,0,0);EnableWindow(Control(id),!list.devices.empty());};
        fill(CameraList,cameras_,UiLabel(L"未检测到摄像头",L"No camera found"));fill(MicList,microphones_,UiLabel(L"未检测到麦克风",L"No microphone found"));
        EnableWindow(Control(Camera),!cameras_.devices.empty());EnableWindow(Control(Microphone),!microphones_.devices.empty());
    }
    void ToggleCamera(){
        expectCamera_=false;
        if(screen_.IsCapturing()){screen_.Stop();call_.SetVideoSource("off");SetWindowTextW(Control(ShareScreen),UiLabel(L"共享主屏幕",L"Share screen"));}
        if(capture_->IsCameraCapturing()){capture_->StopCamera();call_.SetVideoSource("off");video_.Clear();SetWindowTextW(Control(Camera),UiLabel(L"开启摄像头",L"Start camera"));status_=UiLabel(L"摄像头已关闭",L"Camera off");}
        else {
            auto index=SendMessageW(Control(CameraList),CB_GETCURSEL,0,0);if(index<0||size_t(index)>=cameras_.devices.size())return;
            cameraView_=true;CloseFile();videoCount_=0;media::CameraCaptureConfig config;config.device_id=cameras_.devices[index].id;
            auto callback=[this](VideoFrame frame){video_.Put(frame);call_.Video(frame);++videoCount_;};
            auto result=capture_->StartCamera(config,callback);
            if(!result.IsOk()){config.width=640;config.height=480;result=capture_->StartCamera(config,callback);}
            if(result.IsOk()){expectCamera_=true;call_.SetVideoSource("camera");SetWindowTextW(Control(Camera),UiLabel(L"关闭摄像头",L"Stop camera"));status_=UiLabel(L"摄像头已开启 · 等待第一帧",L"Camera started. Waiting for video.");}
            else status_=UiLabel(L"摄像头无法开启：",L"Unable to start camera: ")+Wide(result.Message())+UiLabel(L"。请检查系统隐私权限或关闭占用它的程序。",L". Check device permissions and other apps.");
        }
        EnableWindow(Control(CameraList),!capture_->IsCameraCapturing()&&!cameras_.devices.empty());
        InvalidateRect(surface_,nullptr,FALSE);InvalidateRect(window_,nullptr,FALSE);
    }
    void ToggleMicrophone(){
        expectMicrophone_=false;
        if(capture_->IsMicrophoneCapturing()){monitor_=false;capture_->StopMicrophone();audio_.Close();peak_=0;SetWindowTextW(Control(Microphone),UiLabel(L"开启麦克风",L"Start microphone"));SetWindowTextW(Control(Monitor),UiLabel(L"监听：关闭",L"Monitoring off"));status_=UiLabel(L"麦克风已关闭",L"Microphone off");}
        else {
            auto index=SendMessageW(Control(MicList),CB_GETCURSEL,0,0);if(index<0||size_t(index)>=microphones_.devices.size())return;
            media::AudioCaptureConfig config;config.device_id=microphones_.devices[index].id;config.channels=1;audioCount_=0;audioFailed_=false;
            auto result=capture_->StartMicrophone(config,[this](AudioFrame f){peak_=Peak(f);call_.Audio(f);if(aiCallActive_)ai_.session.Submit("local",f);++audioCount_;if(monitor_&&!audio_.Push(f))audioFailed_=true;});
            if(result.IsOk()){expectMicrophone_=true;SetWindowTextW(Control(Microphone),UiLabel(L"关闭麦克风",L"Stop microphone"));status_=UiLabel(L"麦克风已开启 · 戴上耳机后可开启监听",L"Microphone started. Use headphones for monitoring.");}else status_=UiLabel(L"麦克风无法开启：",L"Unable to start microphone: ")+Wide(result.Message());
        }
        EnableWindow(Control(MicList),!capture_->IsMicrophoneCapturing()&&!microphones_.devices.empty());EnableWindow(Control(Monitor),capture_->IsMicrophoneCapturing());InvalidateRect(window_,nullptr,FALSE);
    }
    void OpenFile(){
        wchar_t path[32768]{};OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=window_;ofn.lpstrFilter=ChineseUi()?L"视频与音频\0*.mp4;*.mov;*.wmv;*.avi;*.m4v;*.mp3;*.wav;*.m4a;*.wma\0所有文件\0*.*\0":L"Video and audio\0*.mp4;*.mov;*.wmv;*.avi;*.m4v;*.mp3;*.wav;*.m4a;*.wma\0All files\0*.*\0";ofn.lpstrFile=path;ofn.nMaxFile=32768;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&ofn))return;
        OpenMedia(path);
    }
    void OpenMedia(const wchar_t* path){
        screen_.Stop();call_.SetVideoSource("off");SetWindowTextW(Control(ShareScreen),UiLabel(L"共享主屏幕",L"Share screen"));
        expectCamera_=false;capture_->StopCamera();SetWindowTextW(Control(Camera),UiLabel(L"开启摄像头",L"Start camera"));
        EnableWindow(Control(CameraList),!cameras_.devices.empty());video_.Clear();
        CloseFile();cameraView_=false;fileName_=std::filesystem::path(path).filename().wstring();playback_=std::make_shared<PlaybackState>();
        auto* cb=new PlayerEvents(playback_);auto hr=MFPCreateMediaPlayer(nullptr,FALSE,0,cb,surface_,&player_);cb->Release();
        if(SUCCEEDED(hr)){player_->SetVolume(volume_/100.f);hr=player_->CreateMediaItemFromURL(path,FALSE,0,nullptr);}
        status_=SUCCEEDED(hr)?UiLabel(L"正在打开：",L"Opening: ")+fileName_:UiLabel(L"媒体无法打开：",L"Unable to open media: ")+Hr(hr);
        if(FAILED(hr))CloseFile();SetWindowTextW(Control(Pause),UiLabel(L"暂停",L"Pause"));InvalidateRect(surface_,nullptr,FALSE);InvalidateRect(window_,nullptr,FALSE);
    }
    void TestAudio(){
        if(monitor_){status_=UiLabel(L"请先关闭麦克风监听，再测试扬声器",L"Stop monitoring before testing speakers");return;}
        AudioFrame f;f.format=AudioSampleFormat::S16;f.sample_rate=48000;f.channels=1;f.data.resize(4800*2);
        for(int i=0;i<4800;++i){double envelope=std::min({1.,i/240.,(4799-i)/240.});auto sample=static_cast<int16_t>(std::sin(i*440.*6.283185307179586/48000.)*10000.*envelope);std::memcpy(f.data.data()+i*2,&sample,2);}
        audio_.Close();status_=audio_.Push(f)?UiLabel(L"已播放 440 Hz 测试音 · 请确认当前系统输出设备",L"Test tone played. Check your selected speakers."):UiLabel(L"音频输出失败 · 请检查 Windows 声音设置",L"Audio output failed. Check sound settings.");InvalidateRect(window_,nullptr,FALSE);
    }
    AiPanel& ActiveAssistant(){return meetingAiContext_&&meetingPage_?meetingPage_->Assistant():ai_;}
    void Layout(){
        RECT rc;GetClientRect(window_,&rc);width_=MulDiv(rc.right,96,dpi_);height_=MulDiv(rc.bottom,96,dpi_);
        if(meetingPage_){meetingPage_->SetDpi(dpi_);ShowWindow(meetingPage_->Handle(),meetingView_?SW_SHOW:SW_HIDE);meetingPage_->Assistant().Hide();}
        ai_.Hide();
        if(fullscreen_&&meetingView_&&meetingPage_){MoveWindow(meetingPage_->Handle(),0,0,rc.right,rc.bottom,TRUE);ShowWindow(surface_,SW_HIDE);ShowWindow(remoteSurface_,SW_HIDE);for(auto c:controls_)if(c)ShowWindow(c,SW_HIDE);return;}
        if(fullscreen_&&!aiView_){MoveWindow(surface_,0,0,rc.right,rc.bottom,TRUE);ShowWindow(remoteSurface_,SW_HIDE);for(auto c:controls_)if(c)ShowWindow(c,SW_HIDE);return;}
        ShowWindow(remoteSurface_,SW_SHOW);for(auto c:controls_)if(c)ShowWindow(c,SW_SHOW);ShowWindow(Control(RemoteMode),SW_SHOW);
        Place(RemoteMode,12,106,left_-24,42);Place(Meeting,12,158,left_-24,42);Place(Ai,12,210,left_-24,42);
        if(aiView_||meetingView_){
            for(int id=Open;id<=Advanced;++id)if(id!=RemoteMode&&id!=Meeting&&id!=Ai)ShowWindow(Control(id),SW_HIDE);
            ShowWindow(surface_,SW_HIDE);ShowWindow(remoteSurface_,SW_HIDE);
            if(meetingView_&&meetingPage_){auto r=R(left_,40,width_-left_,height_-40);MoveWindow(meetingPage_->Handle(),r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);}
            else{auto& assistant=ActiveAssistant();assistant.Open(window_,true);assistant.Place(R(left_,40,width_-left_,height_-40));}
            InvalidateRect(window_,nullptr,FALSE);return;
        }
        ai_.Hide();ShowWindow(surface_,SW_SHOW);
        const int x=left_+16, end=width_-right_-16, total=end-x, deck=(total-16)/2;
        const int deckHeight=std::clamp(deck*9/16,180,std::max(180,height_-534));
        preview_=R(x,130,deck,deckHeight);remotePreview_=R(x+deck+16,130,deck,deckHeight);
        MoveWindow(surface_,preview_.left,preview_.top,preview_.right-preview_.left,preview_.bottom-preview_.top,TRUE);
        MoveWindow(remoteSurface_,remotePreview_.left,remotePreview_.top,remotePreview_.right-remotePreview_.left,remotePreview_.bottom-remotePreview_.top,TRUE);
        lower_=130+deckHeight+66;
        const int sourceWidth=(total-16)/2,mx=x+sourceWidth+16,rx=width_-right_+16;
        Place(Open,width_-right_-174,52,158,32);Place(CameraMode,x,130+deckHeight+10,94,32);Place(FileMode,x+102,130+deckHeight+10,94,32);
        Place(Pause,x+206,130+deckHeight+10,74,32);Place(Stop,x+288,130+deckHeight+10,74,32);Place(FullScreen,end-112,130+deckHeight+10,112,32);
        Place(CameraList,x+12,lower_+66,sourceWidth-24,160);Place(Camera,x+12,lower_+104,sourceWidth-128,34);Place(Refresh,x+sourceWidth-108,lower_+104,96,34);
        Place(MicList,x+12,lower_+182,sourceWidth-24,160);Place(Microphone,x+12,lower_+220,sourceWidth-24,34);Place(ShareScreen,x+12,lower_+262,sourceWidth-24,28);
        Place(Monitor,mx+12,lower_+118,sourceWidth-24,34);Place(Volume,mx+12,lower_+204,sourceWidth-24,30);Place(TestSound,mx+12,lower_+250,sourceWidth-24,34);
        Place(Identity,rx,174,right_-32,34);Place(Room,rx,250,right_-32,34);Place(Advanced,rx,298,right_-32,28);
        ShowWindow(Control(Host),connectionSettings_?SW_SHOW:SW_HIDE);Place(Host,rx,356,right_-32,30);
        const int connectY=connectionSettings_?402:342;Place(Join,rx,connectY,right_-32,38);
        Place(Peers,rx,connectY+82,right_-32,140);Place(Dial,rx,connectY+126,right_-32,38);
        Place(AcceptCall,rx,connectY+172,(right_-40)/2,34);Place(RejectCall,rx+(right_-40)/2+8,connectY+172,(right_-40)/2,34);
        Place(EndCall,rx,connectY+216,(right_-40)/2,34);Place(Reconnect,rx+(right_-40)/2+8,connectY+216,(right_-40)/2,34);
        ShowWindow(Control(Pause),!cameraView_?SW_SHOW:SW_HIDE);ShowWindow(Control(Stop),!cameraView_?SW_SHOW:SW_HIDE);
        if(player_&&!cameraView_)player_->UpdateVideo();InvalidateRect(window_,nullptr,FALSE);
    }
    void PaintSurface(HWND target,HDC dc){
        RECT r;GetClientRect(target,&r);Fill(dc,r,RGB(5,7,10));const bool remote=target==remoteSurface_;
        if(!remote&&!cameraView_&&player_&&playback_&&playback_->ready){player_->UpdateVideo();return;}
        auto frame=remote?remoteVideo_.Get():(cameraView_?video_.Get():nullptr);
        if(frame){double ratio=std::min(double(r.right)/frame->width,double(r.bottom)/frame->height);int w=int(frame->width*ratio),h=int(frame->height*ratio);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=frame->width;info.bmiHeader.biHeight=-static_cast<LONG>(frame->height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,(r.right-w)/2,(r.bottom-h)/2,w,h,0,0,frame->width,frame->height,frame->data.data(),&info,DIB_RGB_COLORS,SRCCOPY);}
        else {RECT t{S(20),r.bottom/2-S(28),r.right-S(20),r.bottom/2};Text(dc,remote?(call_.State()==CallState::Connected&&call_.RemoteVideoSource()=="off"?UiLabel(L"对方已关闭视频",L"Remote camera is off"):UiLabel(L"等待远端画面",L"Waiting for the other participant")):(cameraView_?UiLabel(L"摄像头尚未开启",L"Your camera is off"):UiLabel(L"本地媒体预览",L"Local media preview")),t,body_,Ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);t.top+=S(34);t.bottom+=S(34);Text(dc,remote?UiLabel(L"选择房间内的对象，接听后开始通话",L"Choose someone in your room to call"):(cameraView_?UiLabel(L"选择下方设备，开启视频输入",L"Select a device below to start video"):UiLabel(L"打开视频或音频文件开始播放",L"Open a video or audio file to play")),t,small_,Muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        if(call_.State()==CallState::Connected){auto caption=ai_.session.CaptionFor(remote?"remote":"local");DrawCaptionOverlay(dc,r,Wide(caption.original),Wide(caption.translated),body_);}
    }
    void Paint(HDC dc){
        RECT all;GetClientRect(window_,&all);Fill(dc,all,Bg);if(fullscreen_)return;
        const int x=left_+16,end=width_-right_-16,total=end-x,deck=(total-16)/2,sourceWidth=(total-16)/2,mx=x+sourceWidth+16,rx=width_-right_+16;
        auto card=[&](int px,int py,int pw,int ph){Fill(dc,R(px,py,pw,ph),Border);Fill(dc,R(px+1,py+1,pw-2,ph-2),Panel);};
        Fill(dc,R(0,40,left_,height_-40),Panel);Fill(dc,R(left_,40,width_-left_,56),Panel);Fill(dc,R(width_-right_,40,right_,height_-40),Panel);
        Fill(dc,R(0,39,width_,1),Border);Fill(dc,R(left_-1,40,1,height_),Border);Fill(dc,R(width_-right_,40,1,height_),Border);Fill(dc,R(left_,95,width_-left_-right_,1),Border);
        DrawIconEx(dc,S(12),S(10),LoadIconW(instance_,MAKEINTRESOURCEW(101)),S(20),S(20),0,nullptr,DI_NORMAL);Label(dc,L"LumaLive",42,6,150,28,body_);Label(dc,UiLabel(L"实时沟通工作台",L"Communication workspace"),232,6,220,28,small_,Muted);
        Label(dc,UiLabel(L"主工作台",L"Workspace"),width_-right_-210,6,110,28,small_,Ink);Label(dc,UiLabel(L"本地 / WebRTC",L"Local connection"),width_-right_-104,6,104,28,small_,Muted);
        Label(dc,UiLabel(L"工作空间",L"Workspace"),20,60,left_-40,24,small_,Muted);
        if(meetingView_){Fill(dc,R(left_,40,width_-left_,height_-40),Bg);return;}
        if(aiView_){Fill(dc,R(left_,40,width_-left_,height_-40),Bg);return;}
        Label(dc,UiLabel(L"视频通话",L"Video calls"),x,50,total-194,36,body_);
        Label(dc,UiLabel(L"我的画面",L"Your preview"),x,100,deck,28,small_,Mint);Label(dc,UiLabel(L"对方画面",L"Other participant"),x+deck+16,100,deck,28,small_,call_.Active()?Mint:Muted);
        RECT border=preview_;InflateRect(&border,1,1);Fill(dc,border,Mint);border=remotePreview_;InflateRect(&border,1,1);Fill(dc,border,call_.Active()?Mint:Border);
        card(x,lower_,sourceWidth,std::max(292,height_-lower_-48));card(mx,lower_,sourceWidth,std::max(292,height_-lower_-48));
        Label(dc,UiLabel(L"输入设备",L"Input devices"),x+12,lower_+8,sourceWidth-24,28,small_,Muted);
        Label(dc,UiLabel(L"摄像头",L"Camera"),x+12,lower_+40,sourceWidth-24,22,small_);Label(dc,UiLabel(L"麦克风",L"Microphone"),x+12,lower_+154,sourceWidth-24,24,small_);
        Label(dc,UiLabel(L"音频",L"Audio"),mx+12,lower_+8,sourceWidth-24,28,small_,Muted);
        Label(dc,UiLabel(L"我的麦克风",L"Your microphone"),mx+12,lower_+42,sourceWidth/2,22,small_);Label(dc,UiLabel(L"对方声音",L"Other participant"),mx+sourceWidth/2+6,lower_+42,sourceWidth/2-18,22,small_);
        const int meterWidth=(sourceWidth-36)/2;const float localPeak=std::clamp(peak_.load(),0.f,1.f),remotePeak=std::clamp(remotePeak_.load(),0.f,1.f);
        Fill(dc,R(mx+12,lower_+76,meterWidth,8),Border);Fill(dc,R(mx+12,lower_+76,int(meterWidth*localPeak),8),RGB(47,207,127));Fill(dc,R(mx+sourceWidth/2+6,lower_+76,meterWidth,8),Border);Fill(dc,R(mx+sourceWidth/2+6,lower_+76,int(meterWidth*remotePeak),8),RGB(47,207,127));
        Label(dc,capture_&&capture_->IsMicrophoneCapturing()?UiLabel(L"正在采集",L"Capturing"):UiLabel(L"输入关闭",L"Input off"),mx+12,lower_+90,meterWidth,20,small_,Muted);Label(dc,remoteAudios_>0?UiLabel(L"已收到音频",L"Receiving audio"):UiLabel(L"等待音频",L"Waiting for audio"),mx+sourceWidth/2+6,lower_+90,meterWidth,20,small_,Muted);
        Label(dc,UiLabel(L"输出音量  ",L"Output volume  ")+std::to_wstring(volume_)+L"%",mx+12,lower_+172,sourceWidth-24,24,body_);
        Label(dc,UiLabel(L"通话连接",L"Call connection"),rx,54,right_-32,30,small_,Ink);
        Label(dc,UiLabel(L"与房间内的成员发起一对一通话",L"Call someone in your room"),rx,104,right_-32,28,small_,Muted);
        Label(dc,UiLabel(L"显示名称",L"Display name"),rx,146,right_-32,22,small_,Muted);
        Label(dc,UiLabel(L"房间号码",L"Room"),rx,222,right_-32,22,small_,Muted);
        if(connectionSettings_)Label(dc,UiLabel(L"服务器地址",L"Server address"),rx,330,right_-32,22,small_,Muted);
        const int connectY=connectionSettings_?402:342;
        Label(dc,UiLabel(L"选择通话对象",L"Choose a participant"),rx,connectY+54,right_-32,22,small_,Muted);
        const wchar_t* state=UiLabel(L"未加入房间",L"Join a room to start");switch(call_.State()){case CallState::Joining:state=UiLabel(L"正在加入",L"Joining room");break;case CallState::Ready:state=UiLabel(L"就绪 · 请选择通话对象",L"Ready. Choose a participant.");break;case CallState::Outgoing:state=UiLabel(L"呼叫中 · 等待对方接听",L"Calling. Waiting for an answer.");break;case CallState::Incoming:state=UiLabel(L"收到来电 · 请接听或拒绝",L"Incoming call. Accept or decline.");break;case CallState::Connecting:state=UiLabel(L"正在连接音视频",L"Connecting audio and video");break;case CallState::Connected:state=UiLabel(L"通话已连接",L"Call connected");break;default:break;}
        Label(dc,state,rx,connectionSettings_?672:620,right_-32,26,body_,call_.State()==CallState::Incoming?RGB(255,187,80):Mint);
        Label(dc,Wide(call_.Remote())+L"   "+std::to_wstring(call_.DurationSeconds())+UiLabel(L" 秒",L" s"),rx,connectionSettings_?704:650,right_-32,24,small_,Muted);
        Fill(dc,R(left_,height_-38,width_-left_,38),Bg);Fill(dc,R(left_,height_-39,width_-left_,1),Border);Label(dc,status_,x,height_-36,width_-x-16,32,small_,Muted);
    }
    // Render only this application's own drawing and controls, without capturing the desktop.
    bool RenderCheck(const std::filesystem::path& path){
        if(!path.is_absolute())return false;
        RECT bounds{};GetClientRect(window_,&bounds);const int w=bounds.right,h=bounds.bottom;
        auto reference=GetDC(window_);auto dc=CreateCompatibleDC(reference);auto bitmap=CreateCompatibleBitmap(reference,w,h);ReleaseDC(window_,reference);
        if(!dc||!bitmap){if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);return false;}
        const auto previous=SelectObject(dc,bitmap);Paint(dc);
        for(auto target:{surface_,remoteSurface_}){RECT r;GetWindowRect(target,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&r),2);auto saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);SetViewportOrgEx(dc,r.left,r.top,nullptr);PaintSurface(target,dc);RestoreDC(dc,saved);}
        for(auto control:controls_){if(!control||(GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE)==0)continue;RECT r;GetWindowRect(control,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&r),2);auto saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);SetViewportOrgEx(dc,r.left,r.top,nullptr);SendMessageW(control,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);RestoreDC(dc,saved);}
        SelectObject(dc,previous);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4);const bool copied=GetDIBits(dc,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS)==h;DeleteObject(bitmap);DeleteDC(dc);if(!copied)return false;
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(&header),sizeof(header));output.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));output.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));return output.good();
    }
    static std::wstring Option(const wchar_t* name){
        // The two diagnostic options accept a Windows path, optionally enclosed in quotes.
        const std::wstring command=GetCommandLineW();size_t at=0;
        auto token=[&](){while(at<command.size()&&iswspace(command[at]))++at;std::wstring value;bool quoted=false;while(at<command.size()){const auto c=command[at++];if(c==L'"'){quoted=!quoted;continue;}if(!quoted&&iswspace(c))break;value+=c;}return value;};
        while(at<command.size()){if(token()==name)return token();}return {};
    }
    void DrawButton(const DRAWITEMSTRUCT& d){
        bool primary=d.CtlID==Open||d.CtlID==Join,selected=(d.CtlID==Ai&&aiView_)||(d.CtlID==RemoteMode&&!aiView_&&!meetingView_)||(d.CtlID==Meeting&&meetingView_)||(d.CtlID==CameraMode&&cameraView_)||(d.CtlID==FileMode&&!cameraView_);bool disabled=(d.itemState&ODS_DISABLED)!=0;
        UiTheme::DrawButton(d,body_,primary,selected,d.CtlID==EndCall||d.CtlID==RejectCall);
    }
    void Fullscreen(){
        if(aiView_&&!fullscreen_)return;
        fullscreen_=!fullscreen_;if(fullscreen_){GetWindowRect(window_,&savedWindow_);savedStyle_=static_cast<DWORD>(GetWindowLongPtrW(window_,GWL_STYLE));SetWindowLongPtrW(window_,GWL_STYLE,savedStyle_&~WS_OVERLAPPEDWINDOW);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&mi);SetWindowPos(window_,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}else{SetWindowLongPtrW(window_,GWL_STYLE,savedStyle_);SetWindowPos(window_,nullptr,savedWindow_.left,savedWindow_.top,savedWindow_.right-savedWindow_.left,savedWindow_.bottom-savedWindow_.top,SWP_FRAMECHANGED|SWP_NOZORDER);}Layout();
    }
    void Command(int id){
        if(id==Advanced){connectionSettings_=!connectionSettings_;Layout();return;}
        if(meetingPage_&&meetingPage_->Active()&&(id==Camera||id==Microphone||id==ShareScreen||id==Join)){status_=L"\u8bf7\u5148\u79bb\u5f00\u4f1a\u8bae\uff0c\u518d\u5f00\u59cb\u901a\u8bdd\u6216\u672c\u5730\u91c7\u96c6";InvalidateRect(window_,nullptr,FALSE);return;}
        switch(id){
        case Ai:if(fullscreen_)Fullscreen();aiView_=true;meetingView_=false;Layout();break;
        case Meeting:{if(fullscreen_)Fullscreen();if(!meetingPage_)meetingPage_=CreateHostedMeeting(instance_,window_);if(meetingPage_){meetingView_=true;aiView_=false;meetingAiContext_=true;Layout();}break;}
        case Open:OpenFile();break;
        case Camera:ToggleCamera();break;
        case ShareScreen:if(screen_.IsCapturing()){screen_.Stop();call_.SetVideoSource("off");video_.Clear();SetWindowTextW(Control(ShareScreen),UiLabel(L"共享主屏幕",L"Share screen"));status_=UiLabel(L"屏幕共享已停止",L"Screen sharing stopped");}else{expectCamera_=false;capture_->StopCamera();SetWindowTextW(Control(Camera),UiLabel(L"开启摄像头",L"Start camera"));EnableWindow(Control(CameraList),!cameras_.devices.empty());CloseFile();cameraView_=true;videoCount_=0;reportedScreenError_.clear();call_.SetVideoSource("off");if(screen_.Start([this](VideoFrame f){video_.Put(f);call_.Video(f);++videoCount_;})){call_.SetVideoSource("screen");SetWindowTextW(Control(ShareScreen),UiLabel(L"停止共享主屏幕",L"Stop sharing"));status_=UiLabel(L"正在共享主屏幕 · 通话接通后对方可见",L"Sharing your screen with the connected participant");}else status_=Wide(screen_.LastError());}break;
        case Microphone:ToggleMicrophone();break;
        case Monitor:monitor_=!monitor_;if(!monitor_)audio_.Close();SetWindowTextW(Control(Monitor),monitor_?UiLabel(L"监听：开启",L"Monitoring on"):UiLabel(L"监听：关闭",L"Monitoring off"));break;
        case TestSound:TestAudio();break;
        case Refresh:if(capture_->IsCameraCapturing()||capture_->IsMicrophoneCapturing())status_=UiLabel(L"请先关闭采集，再刷新设备列表",L"Stop capture before refreshing devices");else{Enumerate();status_=UiLabel(L"设备列表已刷新",L"Devices refreshed");}break;
        case CameraMode:cameraView_=true;if(player_)player_->Pause();paused_=true;break;
        case FileMode:cameraView_=false;if(!player_)OpenFile();else{auto hr=player_->Play();if(FAILED(hr))status_=UiLabel(L"播放失败：",L"Playback failed: ")+Hr(hr);else{paused_=false;SetWindowTextW(Control(Pause),UiLabel(L"暂停",L"Pause"));}}break;
        case RemoteMode:if(fullscreen_)Fullscreen();aiView_=false;meetingView_=false;meetingAiContext_=false;Layout();break;
        case Join:{
            if(call_.Active()){call_.Stop();remoteVideo_.Clear();remoteAudio_.Close();remotePeak_=0;remoteVideos_=0;remoteAudios_=0;SetWindowTextW(Control(Join),UiLabel(L"加入房间",L"Join room"));status_=UiLabel(L"已离开房间",L"Left the room");EnableWindow(Control(Host),TRUE);EnableWindow(Control(Room),TRUE);EnableWindow(Control(Identity),TRUE);break;}
            wchar_t host[256]{},room[128]{};GetWindowTextW(Control(Host),host,256);GetWindowTextW(Control(Room),room,128);
            auto utf8=[](const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,' ');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;};
            auto endpoint=utf8(host);auto pos=endpoint.rfind(':');int port=9000;
            if(pos!=std::string::npos){try{size_t consumed=0;auto portText=endpoint.substr(pos+1);port=std::stoi(portText,&consumed);if(consumed!=portText.size())port=0;}catch(...){port=0;}endpoint.resize(pos);}
            if(endpoint.empty()||port<1||port>65535||!*room){status_=UiLabel(L"请填写有效服务器地址、端口和房间",L"Enter a valid server, port and room");break;}
            webrtc::WebRtcCallbacks cb;remoteVideos_=0;remoteAudios_=0;
            cb.on_remote_video=[this](VideoFrame f){remoteVideo_.Put(f);++remoteVideos_;};
            cb.on_remote_audio=[this](AudioFrame f){remotePeak_=Peak(f);if(aiCallActive_)ai_.session.Submit("remote",f);if(!remoteAudio_.Push(f))audioFailed_=true;++remoteAudios_;};
            wchar_t identity[128]{};GetWindowTextW(Control(Identity),identity,128);const auto peer=utf8(identity);if(peer.empty()){status_=UiLabel(L"请输入参与者编号",L"Enter your display name");break;}
            if(call_.Start(endpoint,static_cast<uint16_t>(port),utf8(room),peer,std::move(cb))){SetWindowTextW(Control(Join),UiLabel(L"离开房间",L"Leave room"));EnableWindow(Control(Host),FALSE);EnableWindow(Control(Room),FALSE);EnableWindow(Control(Identity),FALSE);status_=UiLabel(L"正在加入房间，成功后选择对象发起通话",L"Joining room. Choose a participant once connected.");}else status_=UiLabel(L"无法加入房间 · 请先启动信令服务器，并检查地址及端口",L"Unable to join. Check that the server is running and the address is correct.");
            break;
        }
        case Reconnect:call_.Reconnect();status_=CallMessage(call_.LastStatus());break;
        case Dial:{auto index=SendMessageW(Control(Peers),CB_GETCURSEL,0,0);if(index>=0&&size_t(index)<shownPeers_.size())call_.Call(shownPeers_[index]);status_=CallMessage(call_.LastStatus());break;}
        case AcceptCall:call_.Accept();status_=CallMessage(call_.LastStatus());break;
        case RejectCall:call_.Reject();status_=CallMessage(call_.LastStatus());break;
        case EndCall:call_.Hangup();status_=CallMessage(call_.LastStatus());break;
        case Pause:if(player_&&!cameraView_){MFP_MEDIAPLAYER_STATE s;player_->GetState(&s);auto hr=s==MFP_MEDIAPLAYER_STATE_PLAYING?player_->Pause():player_->Play();if(FAILED(hr))status_=UiLabel(L"播放控制失败：",L"Playback failed: ")+Hr(hr);paused_=s==MFP_MEDIAPLAYER_STATE_PLAYING;SetWindowTextW(Control(Pause),paused_?UiLabel(L"继续",L"Resume"):UiLabel(L"暂停",L"Pause"));}break;
        case Stop:CloseFile();fileName_=UiLabel(L"尚未打开媒体",L"No media selected");status_=UiLabel(L"媒体播放已停止",L"Playback stopped");break;
        case FullScreen:Fullscreen();break;
        }
        InvalidateRect(window_,nullptr,FALSE);InvalidateRect(surface_,nullptr,FALSE);for(int i:{CameraMode,FileMode,RemoteMode})InvalidateRect(Control(i),nullptr,TRUE);
    }
    void Shutdown(){if(closing_)return;closing_=true;aiCallActive_=false;meetingPage_.reset();ai_.Close();KillTimer(window_,1);monitor_=false;screen_.Stop();if(capture_){capture_->StopCamera();capture_->StopMicrophone();capture_->Stop();}call_.Stop();remoteAudio_.Close();CloseFile();audio_.Close();}
    LRESULT Handle(UINT message,WPARAM wp,LPARAM lp){
        switch(message){
        case RequestMeetingMedia:
            if(call_.Active())return FALSE;
            if(capture_&&capture_->IsCameraCapturing())ToggleCamera();
            if(capture_&&capture_->IsMicrophoneCapturing())ToggleMicrophone();
            if(screen_.IsCapturing())Command(ShareScreen);
            CloseFile();audio_.Close();return TRUE;
        case OpenMeetingAssistant:meetingAiContext_=true;Command(Ai);return 0;
        case ToggleWorkspaceFullscreen:Fullscreen();return 0;
        case WM_SIZE:Layout();return 0;
        case WM_GETMINMAXINFO:{auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={S(1404),S(830)};return 0;}
        case WM_DPICHANGED:{dpi_=HIWORD(wp);Fonts();auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window_,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);Layout();return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(window_,&ps);RECT r;GetClientRect(window_,&r);auto mem=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(mem,bmp);Paint(mem);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);EndPaint(window_,&ps);return 0;}
        case WM_DRAWITEM:DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
        case WM_CTLCOLORLISTBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(wp),Ink);SetBkColor(reinterpret_cast<HDC>(wp),Panel);return reinterpret_cast<LRESULT>(panelBrush_);
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)Command(LOWORD(wp));return 0;
        case WM_HSCROLL:if(reinterpret_cast<HWND>(lp)==Control(Volume)){volume_=static_cast<int>(SendMessageW(Control(Volume),TBM_GETPOS,0,0));audio_.SetVolume(volume_/100.f);remoteAudio_.SetVolume(volume_/100.f);if(player_)player_->SetVolume(volume_/100.f);InvalidateRect(window_,nullptr,FALSE);}return 0;
        case WM_TIMER:{
            if(expectCamera_&&!capture_->IsCameraCapturing()){expectCamera_=false;capture_->StopCamera();call_.SetVideoSource("off");video_.Clear();SetWindowTextW(Control(Camera),UiLabel(L"开启摄像头",L"Start camera"));EnableWindow(Control(CameraList),!cameras_.devices.empty());status_=UiLabel(L"摄像头采集意外停止，请检查设备后重新开启",L"Camera disconnected. Check the device and restart it.");}
            if(expectMicrophone_&&!capture_->IsMicrophoneCapturing()){expectMicrophone_=false;capture_->StopMicrophone();monitor_=false;audio_.Close();peak_=0;SetWindowTextW(Control(Microphone),UiLabel(L"开启麦克风",L"Start microphone"));SetWindowTextW(Control(Monitor),UiLabel(L"监听：关闭",L"Monitoring off"));EnableWindow(Control(Monitor),FALSE);EnableWindow(Control(MicList),!microphones_.devices.empty());status_=UiLabel(L"麦克风采集意外停止，请检查设备后重新开启",L"Microphone disconnected. Check the device and restart it.");}
            remotePeak_.store(remotePeak_.load()*0.9f);
            if(call_.Active()){auto state=call_.Poll();if(!state.empty())status_=CallMessage(state);}
            if(shownPeers_!=call_.Participants()){std::wstring selected;wchar_t name[256]{};GetWindowTextW(Control(Peers),name,256);selected=name;shownPeers_=call_.Participants();SendMessageW(Control(Peers),CB_RESETCONTENT,0,0);int selectedIndex=0;for(size_t i=0;i<shownPeers_.size();++i){auto name=Wide(shownPeers_[i]);SendMessageW(Control(Peers),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(name==selected)selectedIndex=static_cast<int>(i);}SendMessageW(Control(Peers),CB_SETCURSEL,selectedIndex,0);}
            const auto cs=call_.State();aiCallActive_=cs==CallState::Connected;if(cs!=shownCallState_){if((cs==CallState::Outgoing||cs==CallState::Incoming))ai_.session.Reset();if(cs==CallState::Ready||cs==CallState::Offline)ai_.session.Enable(false);if(cs==CallState::Ready||cs==CallState::Offline){remoteVideo_.Clear();remoteAudio_.Close();remotePeak_=0;remoteVideos_=0;remoteAudios_=0;}shownCallState_=cs;SetWindowTextW(Control(RemoteMode),cs==CallState::Incoming?UiLabel(L"\u6765\u7535\u5f85\u63a5\u542c",L"Incoming call"):UiLabel(L"\u89c6\u9891\u901a\u8bdd",L"Video calls"));InvalidateRect(window_,nullptr,FALSE);}
            EnableWindow(Control(Dial),cs==CallState::Ready&&!shownPeers_.empty());EnableWindow(Control(Peers),cs==CallState::Ready);EnableWindow(Control(AcceptCall),cs==CallState::Incoming);EnableWindow(Control(RejectCall),cs==CallState::Incoming);EnableWindow(Control(EndCall),cs==CallState::Outgoing||cs==CallState::Connecting||cs==CallState::Connected);SetWindowTextW(Control(EndCall),cs==CallState::Outgoing?UiLabel(L"取消呼叫",L"Cancel call"):UiLabel(L"结束通话",L"End call"));
            EnableWindow(Control(Reconnect),cs==CallState::Connected||(cs==CallState::Connecting&&call_.DurationSeconds()>0));
            if(call_.RemoteVideoSource()=="off")remoteVideo_.Clear();
            const auto screenError=screen_.LastError();if(!screen_.IsCapturing()&&!screenError.empty()&&screenError!=reportedScreenError_){reportedScreenError_=screenError;call_.SetVideoSource("off");video_.Clear();SetWindowTextW(Control(ShareScreen),UiLabel(L"共享主屏幕",L"Share screen"));status_=Wide(screenError);}
            EnableWindow(Control(Identity),!call_.Active());EnableWindow(Control(Host),!call_.Active());EnableWindow(Control(Room),!call_.Active());SetWindowTextW(Control(Join),call_.Active()?UiLabel(L"离开房间",L"Leave room"):UiLabel(L"加入 / 重新连接",L"Join room"));
            if(playback_){auto error=playback_->error.exchange(S_OK);if(FAILED(error))status_=UiLabel(L"播放失败：",L"Playback failed: ")+Hr(error)+UiLabel(L"。请检查文件或 Windows 媒体解码支持。",L". Check the file and media support.");else if(playback_->ended.exchange(false)){status_=UiLabel(L"播放结束：",L"Playback ended: ")+fileName_;SetWindowTextW(Control(Pause),UiLabel(L"重新播放",L"Replay"));}}
            if(audioFailed_.exchange(false))status_=UiLabel(L"音频输出暂不可用或过载 · 请检查输出设备",L"Audio output unavailable. Check your speakers.");
            EnableWindow(Control(Pause),player_&&!cameraView_);EnableWindow(Control(Stop),bool(player_));
            if(cameraView_||!player_||!playback_||!playback_->ready)InvalidateRect(surface_,nullptr,FALSE);InvalidateRect(remoteSurface_,nullptr,FALSE);
            RECT detail=R(left_+16,lower_,width_-left_-right_-32,130);InvalidateRect(window_,&detail,FALSE);
            RECT r=R(width_-right_+16,180,right_-32,56);InvalidateRect(window_,&r,FALSE);r=R(width_-right_+16,600,right_-32,90);InvalidateRect(window_,&r,FALSE);r=R(0,height_-39,width_,39);InvalidateRect(window_,&r,FALSE);return 0;}
        case WM_CLOSE:if(meetingPage_&&!meetingPage_->CanClose()){Command(Meeting);return 0;}Shutdown();DestroyWindow(window_);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window_,message,wp,lp);
    }
    static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<StudioWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<StudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}return self?self->Handle(m,w,l):DefWindowProcW(h,m,w,l);}
    static LRESULT CALLBACK SurfaceProc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<StudioWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<StudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(self){if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);self->PaintSurface(h,dc);EndPaint(h,&ps);return 0;}if(m==WM_SIZE&&h==self->surface_&&self->player_&&!self->cameraView_)self->player_->UpdateVideo();}return DefWindowProcW(h,m,w,l);}
public:
    ~StudioWindow(){Shutdown();for(auto f:{body_,small_,title_,brand_})if(f)DeleteObject(f);DeleteObject(panelBrush_);}
    int Run(HINSTANCE instance,int show){
        instance_=instance;WNDCLASSW wc{};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpfnWndProc=Proc;wc.lpszClassName=L"LumaStudioPreview";wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));RegisterClassW(&wc);wc.lpfnWndProc=SurfaceProc;wc.lpszClassName=L"LumaVideoSurface";RegisterClassW(&wc);
        window_=CreateWindowExW(0,L"LumaStudioPreview",L"LumaLive Studio",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,900,nullptr,nullptr,instance,this);if(!window_)return 1;
        dpi_=GetDpiForWindow(window_);Fonts();BOOL dark=TRUE;DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));
        surface_=CreateWindowExW(0,L"LumaVideoSurface",UiLabel(L"视频预览",L"Video preview"),WS_CHILD|WS_VISIBLE,0,0,1,1,window_,nullptr,instance,this);
        remoteSurface_=CreateWindowExW(0,L"LumaVideoSurface",UiLabel(L"远端视频",L"Remote video"),WS_CHILD|WS_VISIBLE,0,0,1,1,window_,nullptr,instance,this);
        Button(Advanced,UiLabel(L"连接设置",L"Connection settings"));Button(Ai,UiLabel(L"AI \u52a9\u624b",L"AI assistant"));Button(Meeting,UiLabel(L"\u89c6\u9891\u4f1a\u8bae",L"Meetings"));Button(Open,UiLabel(L"打开媒体文件",L"Open media"));Button(CameraMode,UiLabel(L"摄像头",L"Camera"));Button(FileMode,UiLabel(L"媒体文件",L"Media"));Button(Camera,UiLabel(L"开启摄像头",L"Start camera"));Button(Refresh,UiLabel(L"刷新设备",L"Refresh"));Button(Microphone,UiLabel(L"开启麦克风",L"Start microphone"));Button(Monitor,UiLabel(L"监听：关闭",L"Monitoring off"));Button(TestSound,UiLabel(L"测试扬声器",L"Test speakers"));Button(Pause,UiLabel(L"暂停",L"Pause"));Button(Stop,UiLabel(L"停止播放",L"Stop playback"));Button(FullScreen,UiLabel(L"全屏预览",L"Fullscreen"));
        Button(RemoteMode,UiLabel(L"\u5b9e\u65f6\u8fde\u7ebf",L"Video calls"));Button(Join,UiLabel(L"加入房间",L"Join room"));Make(Host,L"EDIT",L"127.0.0.1:9000",WS_BORDER|ES_AUTOHSCROLL);Make(Room,L"EDIT",L"luma-demo",WS_BORDER|ES_AUTOHSCROLL);
        Make(Identity,L"EDIT",(L"studio-"+std::to_wstring(GetCurrentProcessId())).c_str(),WS_BORDER|ES_AUTOHSCROLL);Make(Peers,L"COMBOBOX",UiLabel(L"通话对象",L"Participant"),CBS_DROPDOWNLIST|WS_VSCROLL);Button(ShareScreen,UiLabel(L"共享主屏幕",L"Share screen"));Button(Dial,UiLabel(L"发起视频通话",L"Start call"));Button(AcceptCall,UiLabel(L"接听",L"Accept"));Button(RejectCall,UiLabel(L"拒绝",L"Decline"));Button(EndCall,UiLabel(L"结束通话",L"End call"));Button(Reconnect,UiLabel(L"重新连接",L"Reconnect"));for(int id:{Dial,AcceptCall,RejectCall,EndCall,Reconnect})EnableWindow(Control(id),FALSE);
        Make(CameraList,L"COMBOBOX",UiLabel(L"摄像头",L"Camera"),CBS_DROPDOWNLIST|WS_VSCROLL);Make(MicList,L"COMBOBOX",UiLabel(L"麦克风",L"Microphone"),CBS_DROPDOWNLIST|WS_VSCROLL);Make(Volume,TRACKBAR_CLASSW,UiLabel(L"输出音量",L"Output volume"),TBS_HORZ|TBS_NOTICKS);SendMessageW(Control(Volume),TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(Control(Volume),TBM_SETPOS,TRUE,volume_);EnableWindow(Control(Monitor),FALSE);
        audio_.SetVolume(volume_/100.f);remoteAudio_.SetVolume(volume_/100.f);
        capture_=media::CreateDeviceCaptureService();auto result=capture_->Start();if(!result.IsOk())status_=UiLabel(L"设备初始化失败：",L"Device initialization failed: ")+Wide(result.Message());Enumerate();Layout();
        const auto renderPath=Option(L"--render-check");if(!renderPath.empty()){const bool rendered=RenderCheck(renderPath);Shutdown();DestroyWindow(window_);return rendered?0:2;}
        const auto mediaPath=Option(L"--media");if(!mediaPath.empty())OpenMedia(mediaPath.c_str());
        SetTimer(window_,1,33,nullptr);ShowWindow(window_,show);UpdateWindow(window_);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN){if(msg.wParam==VK_F11){Fullscreen();continue;}if(msg.wParam==VK_ESCAPE&&fullscreen_){Fullscreen();continue;}if(msg.wParam=='O'&&(GetKeyState(VK_CONTROL)&0x8000)){OpenFile();continue;}if(msg.wParam==VK_SPACE&&msg.hwnd==window_){Command(Pause);continue;}}if(!IsDialogMessageW(window_,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        return static_cast<int>(msg.wParam);
    }
};
}
int RunStudioPreview(HINSTANCE instance,int show){
    if(std::wstring(GetCommandLineW()).find(L"--meeting")!=std::wstring::npos)return RunMeetingPreview(instance,show);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    int result;{StudioWindow window;result=window.Run(instance,show);}CoUninitialize();return result;
}
}


