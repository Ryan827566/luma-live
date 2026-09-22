#include "StudioPreview.hpp"
#include "PreviewMedia.hpp"
#include "AudioOutput.hpp"
#include "PreviewCall.hpp"
#include "DeviceCaptureFactory.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mfplay.h>
#include <mfapi.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <filesystem>
#include <string>
#include <thread>

namespace luma::client::ui::preview {
namespace {
using Microsoft::WRL::ComPtr;
constexpr COLORREF Bg=RGB(14,18,22), Panel=RGB(23,29,34), Border=RGB(43,53,59), Ink=RGB(233,240,241), Muted=RGB(153,168,176), Mint=RGB(116,226,185);
enum Id {Open=100,Camera,Microphone,Monitor,Volume,TestSound,Refresh,CameraList,MicList,Pause,Stop,FileMode,CameraMode,FullScreen,Join,Host,Room,RemoteMode};
std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
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
    HWND window_{},surface_{};HINSTANCE instance_{};
    HFONT body_{},small_{},title_{},brand_{};HBRUSH panelBrush_{CreateSolidBrush(Panel)};
    std::array<HWND,32> controls_{};
    std::unique_ptr<media::IDeviceCaptureService> capture_;
    media::CaptureDeviceList cameras_,microphones_;
    VideoMailbox video_;AudioOutput audio_;
    VideoMailbox remoteVideo_;AudioOutput remoteAudio_;PreviewCall call_;
    std::atomic<uint64_t> remoteVideos_{0},remoteAudios_{0};
    bool remoteView_{false};
    std::atomic<float> peak_{0};std::atomic<bool> monitor_{false};std::atomic<uint64_t> videoCount_{0},audioCount_{0};
    std::atomic<bool> audioFailed_{false};
    std::shared_ptr<PlaybackState> playback_;
    ComPtr<IMFPMediaPlayer> player_;
    bool cameraView_{true},paused_{false},fullscreen_{false},closing_{false};
    RECT preview_{},savedWindow_{};DWORD savedStyle_{};
    std::wstring status_{L"选择摄像头，或打开一个视频 / 音频文件"},fileName_{L"尚未打开媒体"};
    int width_{1440},height_{900},left_{236},right_{328},volume_{65};
    UINT dpi_{96};
    HWND Control(int id)const{return controls_[id-100];}
    int S(int n)const{return MulDiv(n,dpi_,96);}
    void Place(int id,int x,int y,int w,int h){MoveWindow(Control(id),S(x),S(y),S(w),S(h),TRUE);}
    HWND Make(int id,const wchar_t* cls,const wchar_t* label,DWORD style){auto h=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,1,1,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);controls_[id-100]=h;SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(body_),TRUE);return h;}
    void Button(int id,const wchar_t* label){Make(id,L"BUTTON",label,BS_OWNERDRAW);}
    void Fonts(){for(auto f:{body_,small_,title_,brand_})if(f)DeleteObject(f);auto make=[&](int size,int weight){return CreateFontW(-S(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");};body_=make(14,400);small_=make(12,400);title_=make(22,600);brand_=make(23,700);for(auto c:controls_)if(c)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(body_),TRUE);}
    RECT R(int x,int y,int w,int h)const{return {S(x),S(y),S(x+w),S(y+h)};}
    void Label(HDC dc,const std::wstring& s,int x,int y,int w,int h,HFONT f,COLORREF c=Ink,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){Text(dc,s,R(x,y,w,h),f,c,flags);}
    void CloseFile(){if(playback_)playback_->closing=true;if(player_){player_->Shutdown();player_.Reset();}playback_.reset();paused_=false;}
    void Enumerate(){
        cameras_=capture_->EnumerateDevices(media::CaptureDeviceType::Camera);microphones_=capture_->EnumerateDevices(media::CaptureDeviceType::Microphone);
        auto fill=[&](int id,const auto& list,const wchar_t* empty){SendMessageW(Control(id),CB_RESETCONTENT,0,0);for(auto& d:list.devices)SendMessageW(Control(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Wide(d.name).c_str()));if(list.devices.empty())SendMessageW(Control(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(empty));SendMessageW(Control(id),CB_SETCURSEL,0,0);EnableWindow(Control(id),!list.devices.empty());};
        fill(CameraList,cameras_,L"未检测到摄像头");fill(MicList,microphones_,L"未检测到麦克风");
        EnableWindow(Control(Camera),!cameras_.devices.empty());EnableWindow(Control(Microphone),!microphones_.devices.empty());
    }
    void ToggleCamera(){
        if(capture_->IsCameraCapturing()){capture_->StopCamera();video_.Clear();SetWindowTextW(Control(Camera),L"开启摄像头");status_=L"摄像头已关闭";}
        else {
            auto index=SendMessageW(Control(CameraList),CB_GETCURSEL,0,0);if(index<0||size_t(index)>=cameras_.devices.size())return;
            cameraView_=true;CloseFile();videoCount_=0;media::CameraCaptureConfig config;config.device_id=cameras_.devices[index].id;
            auto callback=[this](VideoFrame frame){video_.Put(frame);call_.Video(frame);++videoCount_;};
            auto result=capture_->StartCamera(config,callback);
            if(!result.IsOk()){config.width=640;config.height=480;result=capture_->StartCamera(config,callback);}
            if(result.IsOk()){SetWindowTextW(Control(Camera),L"关闭摄像头");status_=L"摄像头已开启 · 等待第一帧";}
            else status_=L"摄像头无法开启："+Wide(result.Message())+L"。请检查系统隐私权限或关闭占用它的程序。";
        }
        EnableWindow(Control(CameraList),!capture_->IsCameraCapturing()&&!cameras_.devices.empty());
        InvalidateRect(surface_,nullptr,FALSE);InvalidateRect(window_,nullptr,FALSE);
    }
    void ToggleMicrophone(){
        if(capture_->IsMicrophoneCapturing()){monitor_=false;capture_->StopMicrophone();audio_.Close();peak_=0;SetWindowTextW(Control(Microphone),L"开启麦克风");SetWindowTextW(Control(Monitor),L"监听：关闭");status_=L"麦克风已关闭";}
        else {
            auto index=SendMessageW(Control(MicList),CB_GETCURSEL,0,0);if(index<0||size_t(index)>=microphones_.devices.size())return;
            media::AudioCaptureConfig config;config.device_id=microphones_.devices[index].id;config.channels=1;audioCount_=0;audioFailed_=false;
            auto result=capture_->StartMicrophone(config,[this](AudioFrame f){peak_=Peak(f);call_.Audio(f);++audioCount_;if(monitor_&&!audio_.Push(f))audioFailed_=true;});
            if(result.IsOk()){SetWindowTextW(Control(Microphone),L"关闭麦克风");status_=L"麦克风已开启 · 戴上耳机后可开启监听";}else status_=L"麦克风无法开启："+Wide(result.Message());
        }
        EnableWindow(Control(MicList),!capture_->IsMicrophoneCapturing()&&!microphones_.devices.empty());EnableWindow(Control(Monitor),capture_->IsMicrophoneCapturing());InvalidateRect(window_,nullptr,FALSE);
    }
    void OpenFile(){
        wchar_t path[32768]{};OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=window_;ofn.lpstrFilter=L"视频与音频\0*.mp4;*.mov;*.wmv;*.avi;*.m4v;*.mp3;*.wav;*.m4a;*.wma\0所有文件\0*.*\0";ofn.lpstrFile=path;ofn.nMaxFile=32768;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&ofn))return;
        CloseFile();cameraView_=false;remoteView_=false;fileName_=std::filesystem::path(path).filename().wstring();playback_=std::make_shared<PlaybackState>();
        auto* cb=new PlayerEvents(playback_);auto hr=MFPCreateMediaPlayer(nullptr,FALSE,0,cb,surface_,&player_);cb->Release();
        if(SUCCEEDED(hr)){player_->SetVolume(volume_/100.f);hr=player_->CreateMediaItemFromURL(path,TRUE,0,nullptr);}
        status_=SUCCEEDED(hr)?L"正在打开："+fileName_:L"媒体无法打开："+Hr(hr);
        if(FAILED(hr))CloseFile();SetWindowTextW(Control(Pause),L"暂停");InvalidateRect(surface_,nullptr,FALSE);InvalidateRect(window_,nullptr,FALSE);
    }
    void TestAudio(){
        if(monitor_){status_=L"请先关闭麦克风监听，再测试扬声器";return;}
        AudioFrame f;f.format=AudioSampleFormat::S16;f.sample_rate=48000;f.channels=1;f.data.resize(4800*2);
        for(int i=0;i<4800;++i){double envelope=std::min({1.,i/240.,(4799-i)/240.});auto sample=static_cast<int16_t>(std::sin(i*440.*6.283185307179586/48000.)*10000.*envelope);std::memcpy(f.data.data()+i*2,&sample,2);}
        audio_.Close();status_=audio_.Push(f)?L"已播放 440 Hz 测试音 · 请确认当前系统输出设备":L"音频输出失败 · 请检查 Windows 声音设置";InvalidateRect(window_,nullptr,FALSE);
    }
    void Layout(){
        RECT rc;GetClientRect(window_,&rc);width_=MulDiv(rc.right,96,dpi_);height_=MulDiv(rc.bottom,96,dpi_);
        if(fullscreen_){MoveWindow(surface_,0,0,rc.right,rc.bottom,TRUE);for(auto c:controls_)if(c)ShowWindow(c,SW_HIDE);return;}
        for(auto c:controls_)if(c)ShowWindow(c,SW_SHOW);
        left_=width_<1200?200:236;right_=width_<1200?290:328;int rx=width_-right_+22,cx=left_+24,cw=width_-left_-right_-48;
        preview_=R(cx,162,cw,std::max(160,height_-362));MoveWindow(surface_,preview_.left,preview_.top,preview_.right-preview_.left,preview_.bottom-preview_.top,TRUE);
        Place(Open,width_-194,22,166,38);Place(CameraMode,20,134,left_-40,44);Place(FileMode,20,186,left_-40,44);
        Place(RemoteMode,20,238,left_-40,44);
        Place(CameraList,22,346,left_-44,160);Place(Camera,22,394,left_-44,38);Place(Refresh,22,450,left_-44,34);
        Place(MicList,rx,162,right_-44,180);Place(Microphone,rx,214,right_-44,38);Place(Monitor,rx,310,right_-44,38);
        Place(Volume,rx,426,right_-44,30);Place(TestSound,rx,482,right_-44,38);
        Place(Host,rx,582,right_-44,30);Place(Room,rx,640,right_-44,30);Place(Join,rx,690,right_-44,38);
        Place(Pause,cx,height_-180,100,36);Place(Stop,cx+112,height_-180,100,36);Place(FullScreen,cx+cw-104,height_-180,104,36);
        if(player_)player_->UpdateVideo();InvalidateRect(window_,nullptr,FALSE);
    }
    void PaintSurface(HDC dc){
        RECT r;GetClientRect(surface_,&r);Fill(dc,r,RGB(6,9,11));
        if(!remoteView_&&!cameraView_ && player_ && playback_ && playback_->ready){player_->UpdateVideo();return;}
        auto frame=remoteView_?remoteVideo_.Get():(cameraView_?video_.Get():nullptr);
        if(frame){double ratio=std::min(double(r.right)/frame->width,double(r.bottom)/frame->height);int w=int(frame->width*ratio),h=int(frame->height*ratio);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=frame->width;info.bmiHeader.biHeight=-static_cast<LONG>(frame->height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,(r.right-w)/2,(r.bottom-h)/2,w,h,0,0,frame->width,frame->height,frame->data.data(),&info,DIB_RGB_COLORS,SRCCOPY);}
        else {RECT t{S(24),r.bottom/2-S(45),r.right-S(24),r.bottom/2};Text(dc,remoteView_?L"等待另一端的画面":(cameraView_?L"让画面，出现在这里。":L"你的下一段故事"),t,title_);t.top+=S(48);t.bottom+=S(48);Text(dc,remoteView_?L"两端填写相同的信令服务器和房间，然后加入":(cameraView_?L"开启左侧摄像头，或打开媒体文件开始预览":L"打开本地视频或音频文件，开始播放"),t,body_,Muted);}
    }
    void Paint(HDC dc){
        RECT all;GetClientRect(window_,&all);Fill(dc,all,Bg);if(fullscreen_)return;
        Fill(dc,R(0,80,left_,height_-126),Panel);Fill(dc,R(width_-right_,80,right_,height_-126),Panel);
        Fill(dc,R(0,79,width_,1),Border);Fill(dc,R(0,height_-46,width_,1),Border);
        Label(dc,L"LUMA / LIVE",24,17,208,40,brand_,Mint);Label(dc,L"工作台",left_+24,20,120,36,body_);Label(dc,L"本地预览",left_+144,20,150,36,small_,Muted);
        Label(dc,L"素材来源",22,92,left_-44,28,small_,Muted);Label(dc,L"视频设备",22,304,left_-44,32,body_);
        Label(dc,L"设备仅在你开启后采集",22,506,left_-44,30,small_,Muted);
        int cx=left_+24,cw=width_-left_-right_-48,rx=width_-right_+22;
        Label(dc,remoteView_?L"实时连线":(cameraView_?L"摄像头预览":L"媒体播放器"),cx,98,cw,36,title_);Label(dc,remoteView_?L"REMOTE / WEBRTC":(cameraView_?L"LOCAL PREVIEW":fileName_),cx,134,cw,22,small_,Muted);
        auto frame=video_.Get();std::wstring detail=cameraView_?(frame?std::to_wstring(frame->width)+L" × "+std::to_wstring(frame->height)+L"  ·  已接收 "+std::to_wstring(videoCount_.load())+L" 帧":L"等待视频输入"):L"Windows Media Foundation · 保持原始画面比例";
        if(remoteView_)detail=L"远端视频 "+std::to_wstring(remoteVideos_.load())+L" 帧  ·  音频 "+std::to_wstring(remoteAudios_.load())+L" 包";
        Label(dc,detail,cx,height_-232,cw,24,small_,Muted);
        Label(dc,L"播放与预览",cx,height_-126,cw,24,body_);Label(dc,L"Ctrl+O 打开文件    空格 暂停 / 继续    F11 全屏    Esc 退出全屏",cx,height_-94,cw,24,small_,Muted);
        Label(dc,L"音频控制",rx,98,right_-44,36,title_);Label(dc,L"麦克风输入",rx,136,right_-44,24,small_,Muted);
        const float level=peak_.load();Fill(dc,R(rx,272,right_-44,7),Border);Fill(dc,R(rx,272,int((right_-44)*std::clamp(level,0.f,1.f)),7),level>.9?RGB(255,180,100):Mint);
        Label(dc,capture_&&capture_->IsMicrophoneCapturing()?L"输入电平 · 正在采集":L"输入电平 · 麦克风关闭",rx,284,right_-44,22,small_,Muted);
        Label(dc,L"使用耳机监听，避免扬声器回声",rx,355,right_-44,28,small_,Muted);
        Label(dc,L"输出音量   "+std::to_wstring(volume_)+L"%",rx,394,right_-44,28,body_);
        Label(dc,L"当前 Windows 默认输出设备",rx,458,right_-44,20,small_,Muted);
        Label(dc,L"信令服务器（主机:端口）",rx,554,right_-44,24,small_,Muted);
        Label(dc,L"房间",rx,614,right_-44,24,small_,Muted);
        Label(dc,status_,22,height_-42,width_-44,36,small_,Muted);
    }
    void DrawButton(const DRAWITEMSTRUCT& d){
        bool primary=d.CtlID==Open,selected=(d.CtlID==CameraMode&&cameraView_&&!remoteView_)||(d.CtlID==FileMode&&!cameraView_&&!remoteView_)||(d.CtlID==RemoteMode&&remoteView_);bool disabled=(d.itemState&ODS_DISABLED)!=0;
        auto color=primary?Mint:(selected?RGB(37,62,55):RGB(34,43,49));if(d.itemState&ODS_SELECTED)color=RGB(62,101,86);
        Fill(d.hDC,d.rcItem,color);wchar_t label[128];GetWindowTextW(d.hwndItem,label,128);Text(d.hDC,label,d.rcItem,body_,disabled?RGB(98,112,120):(primary?Bg:Ink),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(d.itemState&ODS_FOCUS){RECT r=d.rcItem;InflateRect(&r,-3,-3);DrawFocusRect(d.hDC,&r);}
    }
    void Fullscreen(){
        fullscreen_=!fullscreen_;if(fullscreen_){GetWindowRect(window_,&savedWindow_);savedStyle_=static_cast<DWORD>(GetWindowLongPtrW(window_,GWL_STYLE));SetWindowLongPtrW(window_,GWL_STYLE,savedStyle_&~WS_OVERLAPPEDWINDOW);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&mi);SetWindowPos(window_,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}else{SetWindowLongPtrW(window_,GWL_STYLE,savedStyle_);SetWindowPos(window_,nullptr,savedWindow_.left,savedWindow_.top,savedWindow_.right-savedWindow_.left,savedWindow_.bottom-savedWindow_.top,SWP_FRAMECHANGED|SWP_NOZORDER);}Layout();
    }
    void Command(int id){
        switch(id){
        case Open:OpenFile();break;
        case Camera:ToggleCamera();break;
        case Microphone:ToggleMicrophone();break;
        case Monitor:monitor_=!monitor_;if(!monitor_)audio_.Close();SetWindowTextW(Control(Monitor),monitor_?L"监听：开启":L"监听：关闭");break;
        case TestSound:TestAudio();break;
        case Refresh:if(capture_->IsCameraCapturing()||capture_->IsMicrophoneCapturing())status_=L"请先关闭采集，再刷新设备列表";else{Enumerate();status_=L"设备列表已刷新";}break;
        case CameraMode:remoteView_=false;cameraView_=true;if(player_)player_->Pause();paused_=true;break;
        case FileMode:remoteView_=false;cameraView_=false;if(!player_)OpenFile();break;
        case RemoteMode:remoteView_=true;if(player_)player_->Pause();break;
        case Join:{
            if(call_.Active()){call_.Stop();remoteVideo_.Clear();remoteAudio_.Close();SetWindowTextW(Control(Join),L"加入房间");status_=L"已离开房间";EnableWindow(Control(Host),TRUE);EnableWindow(Control(Room),TRUE);break;}
            wchar_t host[256]{},room[128]{};GetWindowTextW(Control(Host),host,256);GetWindowTextW(Control(Room),room,128);
            auto utf8=[](const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,' ');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;};
            auto endpoint=utf8(host);auto pos=endpoint.rfind(':');int port=9000;
            if(pos!=std::string::npos){try{size_t consumed=0;auto portText=endpoint.substr(pos+1);port=std::stoi(portText,&consumed);if(consumed!=portText.size())port=0;}catch(...){port=0;}endpoint.resize(pos);}
            if(endpoint.empty()||port<1||port>65535||!*room){status_=L"请填写有效服务器地址、端口和房间";break;}
            webrtc::WebRtcCallbacks cb;remoteVideos_=0;remoteAudios_=0;
            cb.on_remote_video=[this](VideoFrame f){remoteVideo_.Put(f);++remoteVideos_;};
            cb.on_remote_audio=[this](AudioFrame f){remoteAudio_.Push(f);++remoteAudios_;};
            const auto peer="studio-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
            if(call_.Start(endpoint,static_cast<uint16_t>(port),utf8(room),peer,std::move(cb))){remoteView_=true;if(player_)player_->Pause();SetWindowTextW(Control(Join),L"离开房间");EnableWindow(Control(Host),FALSE);EnableWindow(Control(Room),FALSE);status_=L"已加入房间 · 开启摄像头和麦克风即可发送，等待另一端加入";}else status_=L"无法加入房间 · 请先启动信令服务器，并检查地址及端口";
            break;
        }
        case Pause:if(player_&&!cameraView_){MFP_MEDIAPLAYER_STATE s;player_->GetState(&s);auto hr=s==MFP_MEDIAPLAYER_STATE_PLAYING?player_->Pause():player_->Play();if(FAILED(hr))status_=L"播放控制失败："+Hr(hr);paused_=s==MFP_MEDIAPLAYER_STATE_PLAYING;SetWindowTextW(Control(Pause),paused_?L"继续播放":L"暂停");}break;
        case Stop:CloseFile();fileName_=L"尚未打开媒体";status_=L"媒体播放已停止";break;
        case FullScreen:Fullscreen();break;
        }
        InvalidateRect(window_,nullptr,FALSE);InvalidateRect(surface_,nullptr,FALSE);for(int i:{CameraMode,FileMode,RemoteMode})InvalidateRect(Control(i),nullptr,TRUE);
    }
    void Shutdown(){if(closing_)return;closing_=true;KillTimer(window_,1);monitor_=false;if(capture_){capture_->StopCamera();capture_->StopMicrophone();capture_->Stop();}call_.Stop();remoteAudio_.Close();CloseFile();audio_.Close();}
    LRESULT Handle(UINT message,WPARAM wp,LPARAM lp){
        switch(message){
        case WM_SIZE:Layout();return 0;
        case WM_GETMINMAXINFO:{auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={S(1080),S(840)};return 0;}
        case WM_DPICHANGED:{dpi_=HIWORD(wp);Fonts();auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window_,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);Layout();return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(window_,&ps);RECT r;GetClientRect(window_,&r);auto mem=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(mem,bmp);Paint(mem);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);EndPaint(window_,&ps);return 0;}
        case WM_DRAWITEM:DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
        case WM_CTLCOLORLISTBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(wp),Ink);SetBkColor(reinterpret_cast<HDC>(wp),Panel);return reinterpret_cast<LRESULT>(panelBrush_);
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)Command(LOWORD(wp));return 0;
        case WM_HSCROLL:if(reinterpret_cast<HWND>(lp)==Control(Volume)){volume_=static_cast<int>(SendMessageW(Control(Volume),TBM_GETPOS,0,0));audio_.SetVolume(volume_/100.f);remoteAudio_.SetVolume(volume_/100.f);if(player_)player_->SetVolume(volume_/100.f);InvalidateRect(window_,nullptr,FALSE);}return 0;
        case WM_TIMER:{
            if(call_.Active()){auto state=call_.Poll();if(!state.empty())status_=L"连线状态："+Wide(state);}
            if(playback_){auto error=playback_->error.exchange(S_OK);if(FAILED(error))status_=L"播放失败："+Hr(error)+L"。请检查文件或 Windows 媒体解码支持。";else if(playback_->ended.exchange(false)){status_=L"播放结束："+fileName_;SetWindowTextW(Control(Pause),L"重新播放");}}
            if(audioFailed_.exchange(false))status_=L"音频输出暂不可用或过载 · 请检查输出设备";
            EnableWindow(Control(Pause),player_&&!cameraView_);EnableWindow(Control(Stop),bool(player_));
            if(cameraView_||remoteView_)InvalidateRect(surface_,nullptr,FALSE);
            RECT detail=R(left_+24,height_-232,width_-left_-right_-48,24);InvalidateRect(window_,&detail,FALSE);
            RECT r=R(width_-right_+22,272,right_-44,38);InvalidateRect(window_,&r,FALSE);r=R(0,height_-46,width_,46);InvalidateRect(window_,&r,FALSE);return 0;}
        case WM_CLOSE:Shutdown();DestroyWindow(window_);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window_,message,wp,lp);
    }
    static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<StudioWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<StudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}return self?self->Handle(m,w,l):DefWindowProcW(h,m,w,l);}
    static LRESULT CALLBACK SurfaceProc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<StudioWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<StudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(self){if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);self->PaintSurface(dc);EndPaint(h,&ps);return 0;}if(m==WM_SIZE&&self->player_)self->player_->UpdateVideo();}return DefWindowProcW(h,m,w,l);}
public:
    ~StudioWindow(){Shutdown();for(auto f:{body_,small_,title_,brand_})if(f)DeleteObject(f);DeleteObject(panelBrush_);}
    int Run(HINSTANCE instance,int show){
        instance_=instance;WNDCLASSW wc{};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpfnWndProc=Proc;wc.lpszClassName=L"LumaStudioPreview";RegisterClassW(&wc);wc.lpfnWndProc=SurfaceProc;wc.lpszClassName=L"LumaVideoSurface";RegisterClassW(&wc);
        window_=CreateWindowExW(0,L"LumaStudioPreview",L"LumaLive Studio",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,900,nullptr,nullptr,instance,this);if(!window_)return 1;
        dpi_=GetDpiForWindow(window_);Fonts();BOOL dark=TRUE;DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));
        surface_=CreateWindowExW(0,L"LumaVideoSurface",L"视频预览",WS_CHILD|WS_VISIBLE,0,0,1,1,window_,nullptr,instance,this);
        Button(Open,L"打开媒体文件");Button(CameraMode,L"摄像头预览");Button(FileMode,L"媒体播放器");Button(Camera,L"开启摄像头");Button(Refresh,L"刷新设备");Button(Microphone,L"开启麦克风");Button(Monitor,L"监听：关闭");Button(TestSound,L"测试扬声器");Button(Pause,L"暂停");Button(Stop,L"停止播放");Button(FullScreen,L"全屏预览");
        Button(RemoteMode,L"实时连线");Button(Join,L"加入房间");Make(Host,L"EDIT",L"127.0.0.1:9000",WS_BORDER|ES_AUTOHSCROLL);Make(Room,L"EDIT",L"luma-demo",WS_BORDER|ES_AUTOHSCROLL);
        Make(CameraList,L"COMBOBOX",L"摄像头",CBS_DROPDOWNLIST|WS_VSCROLL);Make(MicList,L"COMBOBOX",L"麦克风",CBS_DROPDOWNLIST|WS_VSCROLL);Make(Volume,TRACKBAR_CLASSW,L"输出音量",TBS_HORZ|TBS_NOTICKS);SendMessageW(Control(Volume),TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(Control(Volume),TBM_SETPOS,TRUE,volume_);EnableWindow(Control(Monitor),FALSE);
        capture_=media::CreateDeviceCaptureService();auto result=capture_->Start();if(!result.IsOk())status_=L"设备初始化失败："+Wide(result.Message());Enumerate();Layout();SetTimer(window_,1,33,nullptr);ShowWindow(window_,show);UpdateWindow(window_);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN){if(msg.wParam==VK_F11){Fullscreen();continue;}if(msg.wParam==VK_ESCAPE&&fullscreen_){Fullscreen();continue;}if(msg.wParam=='O'&&(GetKeyState(VK_CONTROL)&0x8000)){OpenFile();continue;}if(msg.wParam==VK_SPACE&&msg.hwnd==window_){Command(Pause);continue;}}if(!IsDialogMessageW(window_,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        return static_cast<int>(msg.wParam);
    }
};
}
int RunStudioPreview(HINSTANCE instance,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    int result;{StudioWindow window;result=window.Run(instance,show);}CoUninitialize();return result;
}
}
