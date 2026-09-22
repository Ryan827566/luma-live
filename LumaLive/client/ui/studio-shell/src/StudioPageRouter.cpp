#include "StudioPageRouter.hpp"
#include <algorithm>
#include <cwchar>

namespace luma::client::ui::studio {
namespace {
constexpr COLORREF CARD=RGB(17,21,26), BORDER=RGB(42,48,56), TEXT=RGB(231,237,242), MUTED=RGB(126,137,149), GREEN=RGB(53,217,145);

void fill(HDC dc, RECT r, COLORREF c) {
    auto brush=CreateSolidBrush(c); FillRect(dc,&r,brush); DeleteObject(brush);
}
void text(HDC dc,const wchar_t* s,RECT r,int size,COLORREF c,bool bold=false,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
    auto font=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,c); DrawTextW(dc,s,-1,&r,flags); SelectObject(dc,old); DeleteObject(font);
}
void box(HDC dc,RECT r,COLORREF bg,COLORREF border) {
    fill(dc,r,bg); auto pen=CreatePen(PS_SOLID,1,border); auto old=SelectObject(dc,pen);
    auto brush=static_cast<HBRUSH>(GetStockObject(NULL_BRUSH)); auto oldBrush=SelectObject(dc,brush);
    Rectangle(dc,r.left,r.top,r.right,r.bottom); SelectObject(dc,oldBrush); SelectObject(dc,old); DeleteObject(pen);
}

class DashboardPage final : public IStudioPage {
public:
    StudioPage id() const noexcept override { return StudioPage::Main; }
    const wchar_t* title() const noexcept override { return L"Preview Deck"; }
    const wchar_t* subtitle() const noexcept override { return L"Program · On Air · 6000 kb/s"; }
    void paint(HDC dc,const RECT& a,const StudioPageContext&) const override {
        const int w=a.right-a.left,h=a.bottom-a.top,gap=10,pw=(w-gap)/2;
        box(dc,{a.left,a.top,a.left+pw,a.top+270},CARD,BORDER);
        box(dc,{a.left+pw+gap,a.top,a.right,a.top+270},CARD,BORDER);
        text(dc,L"PREVIEW · SCENE 1",{a.left+12,a.top+8,a.left+220,a.top+28},10,RGB(190,200,208),true);
        text(dc,L"PROGRAM · ON AIR",{a.left+pw+gap+12,a.top+8,a.left+pw+gap+220,a.top+28},10,RGB(190,200,208),true);
        fill(dc,{a.left+18,a.top+40,a.left+pw-18,a.top+252},RGB(24,32,40));
        fill(dc,{a.left+pw+gap+18,a.top+40,a.right-18,a.top+252},RGB(25,33,42));
        box(dc,{a.left+pw-130,a.top+175,a.left+pw-28,a.top+238},RGB(34,40,49),GREEN);
        text(dc,L"CAM 1",{a.left+pw-122,a.top+182,a.left+pw-42,a.top+204},9,GREEN,true);
        box(dc,{a.left,a.top+280,a.left+pw,a.bottom},CARD,BORDER);
        box(dc,{a.left+pw+gap,a.top+280,a.right,a.bottom},CARD,BORDER);
        text(dc,L"MULTI-CHANNEL AUDIO MIXER",{a.left+12,a.top+292,a.left+250,a.top+318},11,TEXT,true);
        text(dc,L"MAIN BUS    LIVE",{a.left+12,a.top+330,a.left+220,a.top+354},10,GREEN,true);
        const wchar_t* rows[]={L"MIC / AUX",L"SYSTEM B",L"GAME OUT"};
        for(int i=0;i<3;i++){int y=a.top+360+i*42;text(dc,rows[i],{a.left+12,y,a.left+120,y+20},10,RGB(174,183,193));fill(dc,{a.left+12,y+25,a.left+pw-18,y+31},RGB(34,40,49));fill(dc,{a.left+12,y+25,a.left+12+(pw-30)*(i==0?.72:i==1?.48:.62),y+31},RGB(31,191,123));}
        text(dc,L"STREAM & ENCODER",{a.left+pw+gap+12,a.top+292,a.right-20,a.top+318},11,TEXT,true);
        const wchar_t* enc[]={L"STREAM                 ONLINE",L"Video bitrate          6200 kb/s",L"Dropped frames         0.01%",L"Encoder                NVIDIA NVENC",L"Render time            2.1 ms"};
        for(int i=0;i<5;i++) text(dc,enc[i],{a.left+pw+gap+12,a.top+330+i*34,a.right-18,a.top+352+i*34},10,i==0?GREEN:RGB(174,183,193),i==0);
    }
};

class GenericPage final : public IStudioPage {
    StudioPage id_; const wchar_t* title_; const wchar_t* subtitle_;
public:
    GenericPage(StudioPage id,const wchar_t* title,const wchar_t* subtitle):id_(id),title_(title),subtitle_(subtitle){}
    StudioPage id() const noexcept override{return id_;}
    const wchar_t* title() const noexcept override{return title_;}
    const wchar_t* subtitle() const noexcept override{return subtitle_;}
    void paint(HDC dc,const RECT& a,const StudioPageContext&) const override {
        box(dc,{a.left,a.top,a.right,a.bottom},CARD,BORDER);
        text(dc,L"LumaLive UI module",{a.left+22,a.top+22,a.left+340,a.top+55},15,TEXT,true);
        text(dc,subtitle_,{a.left+22,a.top+56,a.right-22,a.top+82},11,MUTED);
        const wchar_t* cards[]={L"Runtime state",L"Controls",L"Inspector",L"Telemetry"};
        const int gap=16,cw=(a.right-a.left-44-3*gap)/4; int y=a.top+105;
        for(int i=0;i<4;i++){int x=a.left+22+i*(cw+gap);box(dc,{x,y,x+cw,y+160},RGB(22,27,33),BORDER);text(dc,cards[i],{x+12,y+12,x+cw-12,y+36},11,TEXT,true);text(dc,L"UI component ready",{x+12,y+58,x+cw-12,y+82},10,MUTED);text(dc,L"Bound to page router",{x+12,y+86,x+cw-12,y+110},10,MUTED);}
    }
};
}

StudioPageRouter::StudioPageRouter() {
    navigation_={StudioPage::Main,StudioPage::Scenes,StudioPage::Media,StudioPage::Audio,StudioPage::WebRtc,StudioPage::Live,StudioPage::Recordings,StudioPage::Diagnostics,StudioPage::Copilot,StudioPage::Devices,StudioPage::Settings,StudioPage::Setup};
    pages_.push_back(std::make_unique<DashboardPage>());
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Scenes,L"Scene Editor",L"Layers · Snap Grid · Safe Areas · Preview Canvas"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Media,L"Media Library",L"Video · Audio · Graphics · Live Captures & Feeds"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Audio,L"Audio Deck",L"Multi-channel mixer · Monitor · Output Router"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::WebRtc,L"WebRTC",L"Live peer connections · RTT · ICE · DTLS/SRTP"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Live,L"Live Streaming",L"Broadcast Station · RTMP · OAuth · Encoder Integrity"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Recordings,L"Recording Catalog",L"Sessions · Replay · Export · File Utilities"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Diagnostics,L"Diagnostics",L"Streaming telemetry · Path analysis · Encoder logs"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Copilot,L"LumaLive Co-Pilot",L"Broadcast orchestration · optimization advisory · executable actions"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Devices,L"Device Orchestration",L"Camera · Microphone · USB · Hardware telemetry"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Settings,L"Configuration Control Centre",L"Canvas · Audio Hardware · Encoder · Network · OAuth"));
    pages_.push_back(std::make_unique<GenericPage>(StudioPage::Setup,L"Initialization Wizard",L"Project Setup · Canvas Specs · Hardware Detect · Output Router · Dry-run"));
}
void StudioPageRouter::navigate(StudioPage p) noexcept {
    if(std::find(navigation_.begin(),navigation_.end(),p)!=navigation_.end()) current_=p;
}
const IStudioPage& StudioPageRouter::page() const noexcept {
    for(const auto& p:pages_) if(p->id()==current_) return *p;
    return *pages_.front();
}
const wchar_t* StudioPageRouter::label(StudioPage p) noexcept {
    switch(p){case StudioPage::Main:return L"MAIN";case StudioPage::Scenes:return L"SCENES";case StudioPage::Media:return L"MEDIA";case StudioPage::Audio:return L"AUDIO";case StudioPage::WebRtc:return L"WEBRTC";case StudioPage::Live:return L"LIVE";case StudioPage::Recordings:return L"RECORDINGS";case StudioPage::Diagnostics:return L"DIAGNOSTICS";case StudioPage::Copilot:return L"AI";case StudioPage::Devices:return L"DEVICES";case StudioPage::Settings:return L"SETTINGS";case StudioPage::Setup:return L"SETUP";} return L"MAIN";
}
}
