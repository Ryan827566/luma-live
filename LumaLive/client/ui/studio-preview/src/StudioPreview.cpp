#include "StudioPreview.hpp"
#include "PreviewMedia.hpp"
#include "AudioOutput.hpp"
#include "PreviewCall.hpp"
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
enum Id {Open=100,Camera,Microphone,Monitor,Volume,TestSound,Refresh,CameraList,MicList,Pause,Stop,FileMode,CameraMode,FullScreen,Join,Host,Room,RemoteMode,Identity,Peers,Dial,AcceptCall,RejectCall,EndCall,ShareScreen};
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
    HWND window_{},surface_{},remoteSurface_{};HINSTANCE instance_{};
    HFONT body_{},small_{},title_{},brand_{};HBRUSH panelBrush_{CreateSolidBrush(Panel)};
    std::array<HWND,32> controls_{};
    std::unique_ptr<media::IDeviceCaptureService> capture_;
    media::CaptureDeviceList cameras_,microphones_;
    media::ScreenCaptureService screen_;
    VideoMailbox video_;AudioOutput audio_;
    VideoMailbox remoteVideo_;AudioOutput remoteAudio_;PreviewCall call_;
    std::atomic<uint64_t> remoteVideos_{0},remoteAudios_{0};
    std::atomic<float> remotePeak_{0};
    std::atomic<float> peak_{0};std::atomic<bool> monitor_{false};std::atomic<uint64_t> videoCount_{0},audioCount_{0};
    std::atomic<bool> audioFailed_{false};
    std::shared_ptr<PlaybackState> playback_;
    ComPtr<IMFPMediaPlayer> player_;
    bool cameraView_{true},paused_{false},fullscreen_{false},closing_{false};
    RECT preview_{},remotePreview_{},savedWindow_{};DWORD savedStyle_{};
    std::wstring status_{L"选择摄像头，或打开一个视频 / 音频文件"},fileName_{L"尚未打开媒体"};
    int width_{1440},height_{900},left_{52},right_{280},lower_{420},volume_{65};
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
        if(screen_.IsCapturing()){screen_.Stop();SetWindowTextW(Control(ShareScreen),L"共享主屏幕");}
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
        OpenMedia(path);
    }
    void OpenMedia(const wchar_t* path){
        CloseFile();cameraView_=false;fileName_=std::filesystem::path(path).filename().wstring();playback_=std::make_shared<PlaybackState>();
        auto* cb=new PlayerEvents(playback_);auto hr=MFPCreateMediaPlayer(nullptr,FALSE,0,cb,surface_,&player_);cb->Release();
        if(SUCCEEDED(hr)){player_->SetVolume(volume_/100.f);hr=player_->CreateMediaItemFromURL(path,FALSE,0,nullptr);}
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
        if(fullscreen_){MoveWindow(surface_,0,0,rc.right,rc.bottom,TRUE);ShowWindow(remoteSurface_,SW_HIDE);for(auto c:controls_)if(c)ShowWindow(c,SW_HIDE);return;}
        ShowWindow(remoteSurface_,SW_SHOW);for(auto c:controls_)if(c)ShowWindow(c,SW_SHOW);ShowWindow(Control(RemoteMode),SW_HIDE);
        const int x=left_+16, end=width_-right_-16, total=end-x, deck=(total-16)/2;
        const int deckHeight=std::clamp(deck*9/16,180,std::max(180,height_-470));
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
        Place(Host,rx,224,right_-32,28);Place(Room,rx,280,right_-32,28);
        Place(Identity,rx,336,right_-32,28);Place(Join,rx,374,right_-32,32);Place(Peers,rx,438,right_-32,140);
        Place(Dial,rx,478,right_-32,32);Place(AcceptCall,rx,518,(right_-40)/2,32);Place(RejectCall,rx+(right_-40)/2+8,518,(right_-40)/2,32);Place(EndCall,rx,558,right_-32,32);
        if(player_&&!cameraView_)player_->UpdateVideo();InvalidateRect(window_,nullptr,FALSE);
    }
    void PaintSurface(HWND target,HDC dc){
        RECT r;GetClientRect(target,&r);Fill(dc,r,RGB(5,7,10));const bool remote=target==remoteSurface_;
        if(!remote&&!cameraView_&&player_&&playback_&&playback_->ready){player_->UpdateVideo();return;}
        auto frame=remote?remoteVideo_.Get():(cameraView_?video_.Get():nullptr);
        if(frame){double ratio=std::min(double(r.right)/frame->width,double(r.bottom)/frame->height);int w=int(frame->width*ratio),h=int(frame->height*ratio);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=frame->width;info.bmiHeader.biHeight=-static_cast<LONG>(frame->height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,(r.right-w)/2,(r.bottom-h)/2,w,h,0,0,frame->width,frame->height,frame->data.data(),&info,DIB_RGB_COLORS,SRCCOPY);}
        else {RECT t{S(20),r.bottom/2-S(28),r.right-S(20),r.bottom/2};Text(dc,remote?L"等待远端画面":(cameraView_?L"摄像头尚未开启":L"本地媒体预览"),t,body_,Ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);t.top+=S(34);t.bottom+=S(34);Text(dc,remote?L"两端加入同一房间，即可实时连线":(cameraView_?L"选择下方设备，开启视频输入":L"打开视频或音频文件开始播放"),t,small_,Muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    }
    void Paint(HDC dc){
        RECT all;GetClientRect(window_,&all);Fill(dc,all,Bg);if(fullscreen_)return;
        const int x=left_+16,end=width_-right_-16,total=end-x,deck=(total-16)/2,sourceWidth=(total-16)/2,mx=x+sourceWidth+16,rx=width_-right_+16;
        auto card=[&](int px,int py,int pw,int ph){Fill(dc,R(px,py,pw,ph),Border);Fill(dc,R(px+1,py+1,pw-2,ph-2),Panel);};
        Fill(dc,R(0,40,left_,height_-40),Panel);Fill(dc,R(left_,40,width_-left_,56),Panel);Fill(dc,R(width_-right_,40,right_,height_-40),Panel);
        Fill(dc,R(0,39,width_,1),Border);Fill(dc,R(left_-1,40,1,height_),Border);Fill(dc,R(width_-right_,40,1,height_),Border);Fill(dc,R(left_,95,width_-left_-right_,1),Border);
        Fill(dc,R(12,13,10,14),Mint);Label(dc,L"LUMALIVE STUDIO",32,6,194,28,body_);Label(dc,L"MAIN WORKSPACE",232,6,220,28,small_,Muted);
        Label(dc,L"主工作台",width_-right_-210,6,110,28,small_,Ink);Label(dc,L"本地 / WebRTC",width_-right_-104,6,104,28,small_,Muted);
        Label(dc,L"◈",14,52,26,32,title_,Mint);
        Label(dc,L"PROJECT:  LumaLive 工作台",x,50,total-194,36,body_);
        Label(dc,L"PREVIEW DECK / 本地预览",x,100,deck,28,small_,Mint);Label(dc,L"REMOTE DECK / 远端连线",x+deck+16,100,deck,28,small_,call_.Active()?Mint:Muted);
        RECT border=preview_;InflateRect(&border,1,1);Fill(dc,border,Mint);border=remotePreview_;InflateRect(&border,1,1);Fill(dc,border,call_.Active()?Mint:Border);
        card(x,lower_,sourceWidth,std::max(292,height_-lower_-48));card(mx,lower_,sourceWidth,std::max(292,height_-lower_-48));
        Label(dc,L"SOURCES / 输入设备",x+12,lower_+8,sourceWidth-24,28,small_,Muted);
        Label(dc,L"摄像头",x+12,lower_+40,sourceWidth-24,22,small_);Label(dc,L"麦克风",x+12,lower_+154,sourceWidth-24,24,small_);
        Label(dc,L"AUDIO MIXER / 音频",mx+12,lower_+8,sourceWidth-24,28,small_,Muted);
        Label(dc,L"MIC / AUX",mx+12,lower_+42,sourceWidth/2,22,small_);Label(dc,L"REMOTE",mx+sourceWidth/2+6,lower_+42,sourceWidth/2-18,22,small_);
        const int meterWidth=(sourceWidth-36)/2;const float localPeak=std::clamp(peak_.load(),0.f,1.f),remotePeak=std::clamp(remotePeak_.load(),0.f,1.f);
        Fill(dc,R(mx+12,lower_+76,meterWidth,8),Border);Fill(dc,R(mx+12,lower_+76,int(meterWidth*localPeak),8),RGB(47,207,127));Fill(dc,R(mx+sourceWidth/2+6,lower_+76,meterWidth,8),Border);Fill(dc,R(mx+sourceWidth/2+6,lower_+76,int(meterWidth*remotePeak),8),RGB(47,207,127));
        Label(dc,capture_&&capture_->IsMicrophoneCapturing()?L"正在采集":L"输入关闭",mx+12,lower_+90,meterWidth,20,small_,Muted);Label(dc,remoteAudios_>0?L"已收到音频":L"等待音频",mx+sourceWidth/2+6,lower_+90,meterWidth,20,small_,Muted);
        Label(dc,L"输出音量  "+std::to_wstring(volume_)+L"%",mx+12,lower_+172,sourceWidth-24,24,body_);
        Label(dc,L"SOURCE INSPECTOR",rx,54,right_-32,30,small_,Ink);
        Label(dc,L"当前本地输入",rx,108,right_-32,22,small_,Muted);Label(dc,cameraView_?(screen_.IsCapturing()?L"共享主屏幕":L"Camera / 摄像头"):fileName_,rx,136,right_-32,28,body_);
        auto frame=video_.Get();Label(dc,cameraView_&&frame?std::to_wstring(frame->width)+L" × "+std::to_wstring(frame->height):L"等待视频输入",rx,166,right_-32,22,small_,Muted);
        Label(dc,L"服务器（主机:端口）",rx,198,right_-32,22,small_,Muted);Label(dc,L"房间",rx,256,right_-32,22,small_,Muted);Label(dc,L"我的参与者编号",rx,312,right_-32,22,small_,Muted);
        Label(dc,L"选择通话对象",rx,410,right_-32,24,small_,Muted);
        const wchar_t* state=L"未加入房间";switch(call_.State()){case CallState::Joining:state=L"正在加入";break;case CallState::Ready:state=L"就绪 · 请选择通话对象";break;case CallState::Outgoing:state=L"呼叫中 · 等待对方接听";break;case CallState::Incoming:state=L"收到来电 · 请接听或拒绝";break;case CallState::Connecting:state=L"正在连接音视频";break;case CallState::Connected:state=L"通话已连接";break;default:break;}
        Label(dc,state,rx,600,right_-32,26,body_,call_.State()==CallState::Incoming?RGB(255,187,80):Mint);
        Label(dc,Wide(call_.Remote())+L"   "+std::to_wstring(call_.DurationSeconds())+L" 秒",rx,630,right_-32,24,small_,Muted);
        Label(dc,L"接收视频 "+std::to_wstring(remoteVideos_.load())+L" 帧 / 音频 "+std::to_wstring(remoteAudios_.load())+L" 包",rx,658,right_-32,24,small_,Muted);
        Label(dc,L"麦克风监听请使用耳机",rx,696,right_-32,26,small_,Muted);
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
        bool primary=d.CtlID==Open||d.CtlID==Join,selected=(d.CtlID==CameraMode&&cameraView_)||(d.CtlID==FileMode&&!cameraView_);bool disabled=(d.itemState&ODS_DISABLED)!=0;
        auto color=primary?Mint:(selected?RGB(15,57,89):RGB(27,33,43));if(d.itemState&ODS_SELECTED)color=RGB(26,90,144);
        Fill(d.hDC,d.rcItem,color);wchar_t label[128];GetWindowTextW(d.hwndItem,label,128);Text(d.hDC,label,d.rcItem,body_,disabled?RGB(98,112,120):Ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(d.itemState&ODS_FOCUS){RECT r=d.rcItem;InflateRect(&r,-3,-3);DrawFocusRect(d.hDC,&r);}
    }
    void Fullscreen(){
        fullscreen_=!fullscreen_;if(fullscreen_){GetWindowRect(window_,&savedWindow_);savedStyle_=static_cast<DWORD>(GetWindowLongPtrW(window_,GWL_STYLE));SetWindowLongPtrW(window_,GWL_STYLE,savedStyle_&~WS_OVERLAPPEDWINDOW);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&mi);SetWindowPos(window_,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}else{SetWindowLongPtrW(window_,GWL_STYLE,savedStyle_);SetWindowPos(window_,nullptr,savedWindow_.left,savedWindow_.top,savedWindow_.right-savedWindow_.left,savedWindow_.bottom-savedWindow_.top,SWP_FRAMECHANGED|SWP_NOZORDER);}Layout();
    }
    void Command(int id){
        switch(id){
        case Open:OpenFile();break;
        case Camera:ToggleCamera();break;
        case ShareScreen:if(screen_.IsCapturing()){screen_.Stop();video_.Clear();SetWindowTextW(Control(ShareScreen),L"共享主屏幕");status_=L"屏幕共享已停止";}else{capture_->StopCamera();SetWindowTextW(Control(Camera),L"开启摄像头");EnableWindow(Control(CameraList),!cameras_.devices.empty());CloseFile();cameraView_=true;videoCount_=0;if(screen_.Start([this](VideoFrame f){video_.Put(f);call_.Video(f);++videoCount_;})){SetWindowTextW(Control(ShareScreen),L"停止共享主屏幕");status_=L"正在共享主屏幕 · 通话接通后对方可见";}else status_=Wide(screen_.LastError());}break;
        case Microphone:ToggleMicrophone();break;
        case Monitor:monitor_=!monitor_;if(!monitor_)audio_.Close();SetWindowTextW(Control(Monitor),monitor_?L"监听：开启":L"监听：关闭");break;
        case TestSound:TestAudio();break;
        case Refresh:if(capture_->IsCameraCapturing()||capture_->IsMicrophoneCapturing())status_=L"请先关闭采集，再刷新设备列表";else{Enumerate();status_=L"设备列表已刷新";}break;
        case CameraMode:cameraView_=true;if(player_)player_->Pause();paused_=true;break;
        case FileMode:cameraView_=false;if(!player_)OpenFile();else{auto hr=player_->Play();if(FAILED(hr))status_=L"播放失败："+Hr(hr);else{paused_=false;SetWindowTextW(Control(Pause),L"暂停");}}break;
        case RemoteMode:break;
        case Join:{
            if(call_.Active()){call_.Stop();remoteVideo_.Clear();remoteAudio_.Close();remotePeak_=0;remoteVideos_=0;remoteAudios_=0;SetWindowTextW(Control(Join),L"加入房间");status_=L"已离开房间";EnableWindow(Control(Host),TRUE);EnableWindow(Control(Room),TRUE);EnableWindow(Control(Identity),TRUE);break;}
            wchar_t host[256]{},room[128]{};GetWindowTextW(Control(Host),host,256);GetWindowTextW(Control(Room),room,128);
            auto utf8=[](const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,' ');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;};
            auto endpoint=utf8(host);auto pos=endpoint.rfind(':');int port=9000;
            if(pos!=std::string::npos){try{size_t consumed=0;auto portText=endpoint.substr(pos+1);port=std::stoi(portText,&consumed);if(consumed!=portText.size())port=0;}catch(...){port=0;}endpoint.resize(pos);}
            if(endpoint.empty()||port<1||port>65535||!*room){status_=L"请填写有效服务器地址、端口和房间";break;}
            webrtc::WebRtcCallbacks cb;remoteVideos_=0;remoteAudios_=0;
            cb.on_remote_video=[this](VideoFrame f){remoteVideo_.Put(f);++remoteVideos_;};
            cb.on_remote_audio=[this](AudioFrame f){remotePeak_=Peak(f);if(!remoteAudio_.Push(f))audioFailed_=true;++remoteAudios_;};
            wchar_t identity[128]{};GetWindowTextW(Control(Identity),identity,128);const auto peer=utf8(identity);if(peer.empty()){status_=L"请输入参与者编号";break;}
            if(call_.Start(endpoint,static_cast<uint16_t>(port),utf8(room),peer,std::move(cb))){SetWindowTextW(Control(Join),L"离开房间");EnableWindow(Control(Host),FALSE);EnableWindow(Control(Room),FALSE);EnableWindow(Control(Identity),FALSE);status_=L"正在加入房间，成功后选择对象发起通话";}else status_=L"无法加入房间 · 请先启动信令服务器，并检查地址及端口";
            break;
        }
        case Dial:{auto index=SendMessageW(Control(Peers),CB_GETCURSEL,0,0);if(index>=0&&size_t(index)<shownPeers_.size())call_.Call(shownPeers_[index]);status_=Wide(call_.LastStatus());break;}
        case AcceptCall:call_.Accept();status_=Wide(call_.LastStatus());break;
        case RejectCall:call_.Reject();status_=Wide(call_.LastStatus());break;
        case EndCall:call_.Hangup();status_=Wide(call_.LastStatus());break;
        case Pause:if(player_&&!cameraView_){MFP_MEDIAPLAYER_STATE s;player_->GetState(&s);auto hr=s==MFP_MEDIAPLAYER_STATE_PLAYING?player_->Pause():player_->Play();if(FAILED(hr))status_=L"播放控制失败："+Hr(hr);paused_=s==MFP_MEDIAPLAYER_STATE_PLAYING;SetWindowTextW(Control(Pause),paused_?L"继续":L"暂停");}break;
        case Stop:CloseFile();fileName_=L"尚未打开媒体";status_=L"媒体播放已停止";break;
        case FullScreen:Fullscreen();break;
        }
        InvalidateRect(window_,nullptr,FALSE);InvalidateRect(surface_,nullptr,FALSE);for(int i:{CameraMode,FileMode,RemoteMode})InvalidateRect(Control(i),nullptr,TRUE);
    }
    void Shutdown(){if(closing_)return;closing_=true;KillTimer(window_,1);monitor_=false;screen_.Stop();if(capture_){capture_->StopCamera();capture_->StopMicrophone();capture_->Stop();}call_.Stop();remoteAudio_.Close();CloseFile();audio_.Close();}
    LRESULT Handle(UINT message,WPARAM wp,LPARAM lp){
        switch(message){
        case WM_SIZE:Layout();return 0;
        case WM_GETMINMAXINFO:{auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={S(1280),S(830)};return 0;}
        case WM_DPICHANGED:{dpi_=HIWORD(wp);Fonts();auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window_,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);Layout();return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(window_,&ps);RECT r;GetClientRect(window_,&r);auto mem=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(mem,bmp);Paint(mem);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);EndPaint(window_,&ps);return 0;}
        case WM_DRAWITEM:DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
        case WM_CTLCOLORLISTBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(wp),Ink);SetBkColor(reinterpret_cast<HDC>(wp),Panel);return reinterpret_cast<LRESULT>(panelBrush_);
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)Command(LOWORD(wp));return 0;
        case WM_HSCROLL:if(reinterpret_cast<HWND>(lp)==Control(Volume)){volume_=static_cast<int>(SendMessageW(Control(Volume),TBM_GETPOS,0,0));audio_.SetVolume(volume_/100.f);remoteAudio_.SetVolume(volume_/100.f);if(player_)player_->SetVolume(volume_/100.f);InvalidateRect(window_,nullptr,FALSE);}return 0;
        case WM_TIMER:{
            remotePeak_.store(remotePeak_.load()*0.9f);
            if(call_.Active()){auto state=call_.Poll();if(!state.empty())status_=L"连线状态："+Wide(state);}
            if(shownPeers_!=call_.Participants()){std::wstring selected;wchar_t name[256]{};GetWindowTextW(Control(Peers),name,256);selected=name;shownPeers_=call_.Participants();SendMessageW(Control(Peers),CB_RESETCONTENT,0,0);int selectedIndex=0;for(size_t i=0;i<shownPeers_.size();++i){auto name=Wide(shownPeers_[i]);SendMessageW(Control(Peers),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(name==selected)selectedIndex=static_cast<int>(i);}SendMessageW(Control(Peers),CB_SETCURSEL,selectedIndex,0);}
            const auto cs=call_.State();if(cs!=shownCallState_){if(cs==CallState::Ready||cs==CallState::Offline){remoteVideo_.Clear();remoteAudio_.Close();remotePeak_=0;remoteVideos_=0;remoteAudios_=0;}shownCallState_=cs;InvalidateRect(window_,nullptr,FALSE);}
            EnableWindow(Control(Dial),cs==CallState::Ready&&!shownPeers_.empty());EnableWindow(Control(Peers),cs==CallState::Ready);EnableWindow(Control(AcceptCall),cs==CallState::Incoming);EnableWindow(Control(RejectCall),cs==CallState::Incoming);EnableWindow(Control(EndCall),cs==CallState::Outgoing||cs==CallState::Connecting||cs==CallState::Connected);SetWindowTextW(Control(EndCall),cs==CallState::Outgoing?L"取消呼叫":L"结束通话");
            EnableWindow(Control(Identity),!call_.Active());EnableWindow(Control(Host),!call_.Active());EnableWindow(Control(Room),!call_.Active());SetWindowTextW(Control(Join),call_.Active()?L"离开房间":L"加入 / 重新连接");
            if(playback_){auto error=playback_->error.exchange(S_OK);if(FAILED(error))status_=L"播放失败："+Hr(error)+L"。请检查文件或 Windows 媒体解码支持。";else if(playback_->ended.exchange(false)){status_=L"播放结束："+fileName_;SetWindowTextW(Control(Pause),L"重新播放");}}
            if(audioFailed_.exchange(false))status_=L"音频输出暂不可用或过载 · 请检查输出设备";
            EnableWindow(Control(Pause),player_&&!cameraView_);EnableWindow(Control(Stop),bool(player_));
            if(cameraView_)InvalidateRect(surface_,nullptr,FALSE);InvalidateRect(remoteSurface_,nullptr,FALSE);
            RECT detail=R(left_+16,lower_,width_-left_-right_-32,130);InvalidateRect(window_,&detail,FALSE);
            RECT r=R(width_-right_+16,180,right_-32,56);InvalidateRect(window_,&r,FALSE);r=R(width_-right_+16,600,right_-32,90);InvalidateRect(window_,&r,FALSE);r=R(0,height_-39,width_,39);InvalidateRect(window_,&r,FALSE);return 0;}
        case WM_CLOSE:Shutdown();DestroyWindow(window_);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window_,message,wp,lp);
    }
    static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<StudioWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<StudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}return self?self->Handle(m,w,l):DefWindowProcW(h,m,w,l);}
    static LRESULT CALLBACK SurfaceProc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<StudioWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<StudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(self){if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);self->PaintSurface(h,dc);EndPaint(h,&ps);return 0;}if(m==WM_SIZE&&h==self->surface_&&self->player_&&!self->cameraView_)self->player_->UpdateVideo();}return DefWindowProcW(h,m,w,l);}
public:
    ~StudioWindow(){Shutdown();for(auto f:{body_,small_,title_,brand_})if(f)DeleteObject(f);DeleteObject(panelBrush_);}
    int Run(HINSTANCE instance,int show){
        instance_=instance;WNDCLASSW wc{};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpfnWndProc=Proc;wc.lpszClassName=L"LumaStudioPreview";RegisterClassW(&wc);wc.lpfnWndProc=SurfaceProc;wc.lpszClassName=L"LumaVideoSurface";RegisterClassW(&wc);
        window_=CreateWindowExW(0,L"LumaStudioPreview",L"LumaLive Studio",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,900,nullptr,nullptr,instance,this);if(!window_)return 1;
        dpi_=GetDpiForWindow(window_);Fonts();BOOL dark=TRUE;DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));
        surface_=CreateWindowExW(0,L"LumaVideoSurface",L"视频预览",WS_CHILD|WS_VISIBLE,0,0,1,1,window_,nullptr,instance,this);
        remoteSurface_=CreateWindowExW(0,L"LumaVideoSurface",L"远端视频",WS_CHILD|WS_VISIBLE,0,0,1,1,window_,nullptr,instance,this);
        Button(Open,L"打开媒体文件");Button(CameraMode,L"摄像头");Button(FileMode,L"媒体文件");Button(Camera,L"开启摄像头");Button(Refresh,L"刷新设备");Button(Microphone,L"开启麦克风");Button(Monitor,L"监听：关闭");Button(TestSound,L"测试扬声器");Button(Pause,L"暂停");Button(Stop,L"停止播放");Button(FullScreen,L"全屏预览");
        Button(RemoteMode,L"实时连线");Button(Join,L"加入房间");Make(Host,L"EDIT",L"127.0.0.1:9000",WS_BORDER|ES_AUTOHSCROLL);Make(Room,L"EDIT",L"luma-demo",WS_BORDER|ES_AUTOHSCROLL);
        Make(Identity,L"EDIT",(L"studio-"+std::to_wstring(GetCurrentProcessId())).c_str(),WS_BORDER|ES_AUTOHSCROLL);Make(Peers,L"COMBOBOX",L"通话对象",CBS_DROPDOWNLIST|WS_VSCROLL);Button(ShareScreen,L"共享主屏幕");Button(Dial,L"发起视频通话");Button(AcceptCall,L"接听");Button(RejectCall,L"拒绝");Button(EndCall,L"结束通话");for(int id:{Dial,AcceptCall,RejectCall,EndCall})EnableWindow(Control(id),FALSE);
        Make(CameraList,L"COMBOBOX",L"摄像头",CBS_DROPDOWNLIST|WS_VSCROLL);Make(MicList,L"COMBOBOX",L"麦克风",CBS_DROPDOWNLIST|WS_VSCROLL);Make(Volume,TRACKBAR_CLASSW,L"输出音量",TBS_HORZ|TBS_NOTICKS);SendMessageW(Control(Volume),TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(Control(Volume),TBM_SETPOS,TRUE,volume_);EnableWindow(Control(Monitor),FALSE);
        audio_.SetVolume(volume_/100.f);remoteAudio_.SetVolume(volume_/100.f);
        capture_=media::CreateDeviceCaptureService();auto result=capture_->Start();if(!result.IsOk())status_=L"设备初始化失败："+Wide(result.Message());Enumerate();Layout();
        const auto renderPath=Option(L"--render-check");if(!renderPath.empty()){const bool rendered=RenderCheck(renderPath);Shutdown();DestroyWindow(window_);return rendered?0:2;}
        const auto mediaPath=Option(L"--media");if(!mediaPath.empty())OpenMedia(mediaPath.c_str());
        SetTimer(window_,1,33,nullptr);ShowWindow(window_,show);UpdateWindow(window_);
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


