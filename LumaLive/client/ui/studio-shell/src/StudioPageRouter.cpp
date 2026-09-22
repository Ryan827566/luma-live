#include "StudioPageRouter.hpp"
#include <algorithm>
#include <array>
#include <cwchar>

namespace luma::client::ui::studio {
namespace {
constexpr COLORREF CARD=RGB(17,21,26), CARD2=RGB(22,27,33), BORDER=RGB(42,48,56);
constexpr COLORREF TEXT=RGB(231,237,242), MUTED=RGB(126,137,149), GREEN=RGB(53,217,145);
constexpr COLORREF RED=RGB(232,17,35), AMBER=RGB(236,171,72), BLUE=RGB(0,120,212);

void fill(HDC dc, RECT r, COLORREF c){auto b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
void line(HDC dc,int x1,int y1,int x2,int y2,COLORREF c){auto p=CreatePen(PS_SOLID,1,c);auto o=SelectObject(dc,p);MoveToEx(dc,x1,y1,nullptr);LineTo(dc,x2,y2);SelectObject(dc,o);DeleteObject(p);}
void text(HDC dc,const wchar_t* s,RECT r,int size,COLORREF c,bool bold=false,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){
 auto f=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 auto o=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);DrawTextW(dc,s,-1,&r,flags);SelectObject(dc,o);DeleteObject(f);
}
void box(HDC dc,RECT r,COLORREF bg,COLORREF border=BORDER){fill(dc,r,bg);auto p=CreatePen(PS_SOLID,1,border);auto o=SelectObject(dc,p);auto b=static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));auto ob=SelectObject(dc,b);Rectangle(dc,r.left,r.top,r.right,r.bottom);SelectObject(dc,ob);SelectObject(dc,o);DeleteObject(p);}
void pill(HDC dc,RECT r,const wchar_t* s,COLORREF bg,COLORREF fg=TEXT){fill(dc,r,bg);text(dc,s,r,9,fg,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
void section(HDC dc,RECT r,const wchar_t* title,const wchar_t* sub){
 box(dc,r,CARD);text(dc,title,{r.left+14,r.top+10,r.right-14,r.top+32},11,TEXT,true);
 if(sub)text(dc,sub,{r.left+14,r.top+34,r.right-14,r.top+55},9,MUTED);
}
void metric(HDC dc,int x,int y,int w,const wchar_t* label,const wchar_t* value,COLORREF valueColor=TEXT){
 text(dc,label,{x,y,x+w,y+19},9,MUTED,true);text(dc,value,{x,y+22,x+w,y+49},16,valueColor,true);
}
void button(HDC dc,RECT r,const wchar_t* s,bool primary=false,bool danger=false){
 box(dc,r,primary?RGB(25,92,65):(danger?RGB(67,22,28):RGB(24,29,35)),primary?GREEN:(danger?RGB(125,40,48):BORDER));
 text(dc,s,r,10,primary?RGB(8,9,12):(danger?RGB(245,180,186):TEXT),true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
void row(HDC dc,RECT r,const wchar_t* left,const wchar_t* right,COLORREF rightColor=TEXT){
 text(dc,left,{r.left+10,r.top,r.left+(r.right-r.left)*.62,r.bottom},10,TEXT);
 text(dc,right,{r.left+(r.right-r.left)*.62,r.top,r.right-10,r.bottom},10,rightColor,true,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
 line(dc,r.left,r.bottom-1,r.right,r.bottom-1,BORDER);
}

class Page final : public IStudioPage {
    StudioPage id_;
    const wchar_t* title_;
    const wchar_t* subtitle_;
public:
    Page(StudioPage id,const wchar_t* t,const wchar_t* s):id_(id),title_(t),subtitle_(s){}
    StudioPage id() const noexcept override{return id_;}
    const wchar_t* title() const noexcept override{return title_;}
    const wchar_t* subtitle() const noexcept override{return subtitle_;}
    void paint(HDC dc,const RECT& a,StudioPageContext& c) const override {
        switch(id_){
        case StudioPage::Main: paintMain(dc,a,c); break;
        case StudioPage::Compact: paintCompact(dc,a,c); break;
        case StudioPage::Scenes: paintScenes(dc,a,c); break;
        case StudioPage::Inspector: paintInspector(dc,a,c); break;
        case StudioPage::Media: paintMedia(dc,a,c); break;
        case StudioPage::Audio: paintAudio(dc,a,c); break;
        case StudioPage::WebRtc: paintWebRtc(dc,a,c); break;
        case StudioPage::Live: paintLive(dc,a,c); break;
        case StudioPage::Recordings: paintRecordings(dc,a,c); break;
        case StudioPage::Diagnostics: paintDiagnostics(dc,a,c); break;
        case StudioPage::Copilot: paintCopilot(dc,a,c); break;
        case StudioPage::Devices: paintDevices(dc,a,c); break;
        case StudioPage::Settings: paintSettings(dc,a,c); break;
        case StudioPage::Setup: paintSetup(dc,a,c); break;
        case StudioPage::Alerts: paintAlerts(dc,a,c); break;
        }
    }
    bool click(int x,int y,const RECT& a,StudioPageContext& c) const override {
        if(id_==StudioPage::Main){
            if(y>=70&&y<230){int n=(y-70)/40;if(n>=0&&n<4){static const wchar_t* s[]={L"Scene 1 - Host Intro",L"Scene 2 - Screen Share + Cam",L"Scene 3 - Full Camera 4K",L"Scene 4 - BRB Overlay"};c.selected_scene=s[n];c.notice=L"Scene selected: "+c.selected_scene;return true;}}
            if(y>a.bottom-80){c.notice=L"Main workspace telemetry refreshed";return true;}
        }
        if(id_==StudioPage::Compact && y>120&&y<260){c.notice=(x<a.left+(a.right-a.left)/3)?L"CUT transition armed":L"Quick transition ready";return true;}
        if(id_==StudioPage::Scenes){
            if(x<a.left+240 && y>65&&y<310){c.selected_scene=L"Selected scene layer";c.notice=L"Layer selection changed";return true;}
            if(x>a.right-250 && y>65&&y<140){c.snap_grid=!c.snap_grid;c.notice=c.snap_grid?L"Snap Grid enabled":L"Snap Grid disabled";return true;}
            if(x>a.right-250 && y>140&&y<215){c.safe_areas=!c.safe_areas;c.notice=c.safe_areas?L"Safe Areas shown":L"Safe Areas hidden";return true;}
            if(x>a.right-250 && y>215&&y<290){c.center_lock=!c.center_lock;c.notice=c.center_lock?L"Center Lock enabled":L"Center Lock disabled";return true;}
        }
        if(id_==StudioPage::Inspector && y>100&&y<260){c.notice=L"Transform property updated · Apply Properties is ready";return true;}
        if(id_==StudioPage::Media && y>90&&y<430){c.selected_asset=L"Selected media asset";c.notice=L"Asset selected · ready to insert into active scene";return true;}
        if(id_==StudioPage::Audio && y>80){
            int ch=(y-80)/92;if(ch>=0&&ch<4){if(x<a.left+120)c.audio_muted[ch]=!c.audio_muted[ch];else if(x<a.left+190)c.audio_solo[ch]=!c.audio_solo[ch];else c.audio_gain[ch]=std::max(0,std::min(100,(x-a.left-200)*100/std::max(1,a.right-a.left-420)));c.notice=L"Audio channel updated";return true;}
        }
        if(id_==StudioPage::WebRtc && y>85&&y<390){c.selected_peer=(y<185?L"John Doe":y<285?L"Jane Smith":L"Guest Speaker 3");c.notice=L"Peer selected · connection details updated";return true;}
        if(id_==StudioPage::Live){
            if(y>80&&y<150){c.notice=L"Live output settings selected";return true;}
            if(y>150&&y<225){c.notice=L"STOP BROADCAST requires confirmation in the final action layer";return true;}
            if(y>225&&y<300){c.broadcast_paused=!c.broadcast_paused;c.notice=c.broadcast_paused?L"Hardware recording paused":L"Hardware recording resumed";return true;}
        }
        if(id_==StudioPage::Recordings && y>90&&y<400){c.selected_recording=std::max(0,std::min(3,(y-90)/72));c.notice=L"Recording selected · replay utilities enabled";return true;}
        if(id_==StudioPage::Diagnostics && y>85){
            if(y<145){c.dropped_frames=0;c.rtt_ms=12;c.jitter_ms=4;c.notice=L"Diagnostics statistics reset";return true;}
            if(y<215){c.notice=L"Diagnostic report prepared";return true;}
            c.notice=L"Telemetry path inspected";return true;
        }
        if(id_==StudioPage::Copilot && y>90){c.notice=(y<180?L"Co-Pilot: optimization action queued":y<270?L"Co-Pilot: PIP scene action queued":L"Co-Pilot: WebRTC recovery action queued");return true;}
        if(id_==StudioPage::Devices && y>90){c.selected_device=(c.selected_device+1)%3;c.notice=L"Hardware device selection changed";return true;}
        if(id_==StudioPage::Settings && y>80&&y<360){c.oauth_locked=!c.oauth_locked;c.notice=c.oauth_locked?L"Endpoint changes locked while live":L"Endpoint editing unlocked";return true;}
        if(id_==StudioPage::Setup){
            if(y>100&&y<190){c.setup_step=std::max(0,c.setup_step-1);c.notice=L"Previous setup step";return true;}
            if(y>190&&y<280){c.setup_step=std::min(4,c.setup_step+1);c.notice=L"Next setup step";return true;}
            if(y>280&&y<350){c.notice=L"Dry-run complete · all required checks passed";return true;}
        }
        if(id_==StudioPage::Alerts && y>80){c.alert_count=std::max(0,c.alert_count-1);c.notice=L"Alert acknowledged";return true;}
        return false;
    }
private:
    static void paintMain(HDC d,const RECT& a,StudioPageContext& c){
        int gap=10,w=(a.right-a.left-gap)/2;section(d,{a.left,a.top,a.left+w,a.top+260},L"PREVIEW DECK",L"Scene preview · Logitech Brio Pro · 1920×1080");
        section(d,{a.left+w+gap,a.top,a.right,a.top+260},L"PROGRAM · ON AIR",L"Live output · 6000 kb/s · 60 FPS");
        fill(d,{a.left+18,a.top+60,a.left+w-18,a.top+242},RGB(27,34,41));fill(d,{a.left+w+gap+18,a.top+60,a.right-18,a.top+242},RGB(29,37,44));
        pill(d,{a.left+w-115,a.top+205,a.left+w-28,a.top+234},L"CAM 1",RGB(22,91,64),GREEN);
        section(d,{a.left,a.top+274,a.left+w,a.bottom},L"MULTI-CHANNEL AUDIO MIXER",L"MAIN BUS · MIC/AUX · SYSTEM B · GAME OUT");
        for(int i=0;i<4;i++){int y=a.top+325+i*48;text(d,(i==0?L"MIC / AUX":i==1?L"SYSTEM B":i==2?L"GAME OUT":L"MONITOR"),{a.left+14,y,a.left+110,y+22},10,TEXT,true);fill(d,{a.left+115,y+7,a.left+w-20,y+15},BORDER);fill(d,{a.left+115,y+7,a.left+115+(w-135)*(c.audio_gain[i]/100.0),y+15},GREEN);}
        section(d,{a.left+w+gap,a.top+274,a.right,a.bottom},L"STREAM & ENCODER",L"Hardware encoding · NVENC · low latency");
        metric(d,a.left+w+gap+14,a.top+330,150,L"STREAM",c.live?L"ONLINE":L"OFFLINE",c.live?GREEN:RED);
        metric(d,a.left+w+gap+174,a.top+330,150,L"BITRATE",L"6200 kb/s");
        row(d,{a.left+w+gap+14,a.top+395,a.right-14,a.top+428},L"Dropped frames",L"0.01%");
        row(d,{a.left+w+gap+14,a.top+428,a.right-14,a.top+461},L"Render time",L"2.1 ms");
        row(d,{a.left+w+gap+14,a.top+461,a.right-14,a.top+494},L"GPU",L"48.3%");
    }
    static void paintCompact(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.right,a.top+115},L"COMPACT OVERVIEW LIVE",L"1920×1080 · LIVE · quick transition deck");
        button(d,{a.left+14,a.top+64,a.left+110,a.top+100},L"CUT",true);
        button(d,{a.left+120,a.top+64,a.left+216,a.top+100},L"LIVE");
        button(d,{a.left+226,a.top+64,a.left+350,a.top+100},L"QUICK TRANS");
        section(d,{a.left,a.top+128,a.left+(a.right-a.left)*.62,a.bottom},L"PROGRAM OUT",L"Brio 4K Feed · Microphone AUX");
        fill(d,{a.left+18,a.top+195,a.left+(a.right-a.left)*.62-18,a.top+445},RGB(27,34,41));
        section(d,{a.left+(a.right-a.left)*.64,a.top+128,a.right,a.bottom},L"SCENES",L"One-click live switching");
        text(d,L"Intro Camera",{a.left+(a.right-a.left)*.64+14,a.top+190,a.right-14,a.top+225},10,GREEN,true);
        text(d,L"Screen Capture",{a.left+(a.right-a.left)*.64+14,a.top+230,a.right-14,a.top+265},10,TEXT);
        text(d,L"Bitrate 6200 kb/s",{a.left+(a.right-a.left)*.64+14,a.top+305,a.right-14,a.top+340},10,TEXT);
        text(d,L"RTT "+std::to_wstring(c.rtt_ms)+L" ms",{a.left+(a.right-a.left)*.64+14,a.top+340,a.right-14,a.top+375},10,MUTED);
    }
    static void paintScenes(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.left+225,a.bottom},L"LAYERS",L"Scene hierarchy");
        const wchar_t* layers[]={L"Overlay Lower Thirds (PNG)",L"Main Game Screen Share",L"Camera Host CloseUp",L"Ambience Noise Gate"};
        for(int i=0;i<4;i++){int y=a.top+68+i*48;box(d,{a.left+12,y,a.left+213,y+38},i==2?RGB(27,50,43):CARD2,i==2?GREEN:BORDER);text(d,layers[i],{a.left+20,y,a.left+205,y+38},9,i==2?GREEN:TEXT,i==2);}
        section(d,{a.left+237,a.top,a.right-230,a.bottom},L"INTERACTIVE SCENE ORCHESTRATION CANVAS",L"X: 120 · Y: 80 · 400×300 px · Scale 1.00x");
        box(d,{a.left+258,a.top+62,a.right-252,a.bottom-32},RGB(12,16,20),BORDER);
        fill(d,{a.left+280,a.top+92,a.right-280,a.bottom-62},RGB(38,46,54));
        box(d,{a.left+330,a.top+125,a.left+500,a.top+230},RGB(25,72,56),GREEN);
        text(d,L"CAMERA HOST CLOSEUP",{a.left+342,a.top+155,a.left+488,a.top+200},10,GREEN,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(c.safe_areas){auto p=CreatePen(PS_DASH,1,AMBER);auto o=SelectObject(d,p);Rectangle(d,a.left+305,a.top+115,a.right-305,a.bottom-48);SelectObject(d,o);DeleteObject(p);}
        section(d,{a.right-215,a.top,a.right,a.top+300},L"ALIGNMENT & SNAP",L"Interactive controls");
        pill(d,{a.right-198,a.top+68,a.right-30,a.top+104},c.snap_grid?L"SNAP GRID · ON":L"SNAP GRID · OFF",c.snap_grid?RGB(24,79,58):CARD2,c.snap_grid?GREEN:MUTED);
        pill(d,{a.right-198,a.top+120,a.right-30,a.top+156},c.safe_areas?L"SAFE AREAS · ON":L"SAFE AREAS · OFF",c.safe_areas?RGB(70,53,24):CARD2,c.safe_areas?AMBER:MUTED);
        pill(d,{a.right-198,a.top+172,a.right-30,a.top+208},c.center_lock?L"CENTER LOCK · ON":L"CENTER LOCK · OFF",c.center_lock?RGB(24,65,94):CARD2,c.center_lock?BLUE:MUTED);
    }
    static void paintInspector(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.right,a.top+95},L"ADVANCED SETTINGS · MAIN LIVE DECK FEED",L"Transform, audio routing and effects");
        section(d,{a.left,a.top+108,a.left+(a.right-a.left)*.58,a.bottom},L"GEOMETRIC TRANSFORMATIONS",L"1920×1080 canvas");
        row(d,{a.left+14,a.top+165,a.left+(a.right-a.left)*.58-14,a.top+198},L"Position",L"X 120 px · Y 80 px");
        row(d,{a.left+14,a.top+198,a.left+(a.right-a.left)*.58-14,a.top+231},L"Dimensions",L"1920 × 1080");
        row(d,{a.left+14,a.top+231,a.left+(a.right-a.left)*.58-14,a.top+264},L"Scale",L"1.00x");
        row(d,{a.left+14,a.top+264,a.left+(a.right-a.left)*.58-14,a.top+297},L"Rotation",L"0.0°");
        section(d,{a.left+(a.right-a.left)*.60,a.top+108,a.right,a.bottom},L"AUDIO ROUTING / EFFECTS",L"Hardware limiter active");
        row(d,{a.left+(a.right-a.left)*.60+14,a.top+165,a.right-14,a.top+198},L"Audio boost",L"+3.5 dB",GREEN);
        row(d,{a.left+(a.right-a.left)*.60+14,a.top+198,a.right-14,a.top+231},L"Sync offset",L"15 ms");
        row(d,{a.left+(a.right-a.left)*.60+14,a.top+231,a.right-14,a.top+264},L"Limiter",L"ACTIVE",GREEN);
        button(d,{a.left+(a.right-a.left)*.60+14,a.top+292,a.right-14,a.top+332},L"APPLY PROPERTIES",true);
        button(d,{a.left+(a.right-a.left)*.60+14,a.top+342,a.right-14,a.top+382},L"RESET TRANSFORMS");
        text(d,c.notice.c_str(),{a.left+18,a.bottom-35,a.right-18,a.bottom-10},9,MUTED);
    }
    static void paintMedia(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.left+205,a.bottom},L"MEDIA LIBRARY",L"Asset categories");
        const wchar_t* cats[]={L"All Assets",L"Video Files (.mp4, .mov)",L"Audio Tracks (.wav, .mp3)",L"Image Graphics (.png, .jpg)",L"Live Captures & Feeds"};
        for(int i=0;i<5;i++)text(d,cats[i],{a.left+16,a.top+70+i*42,a.left+195,a.top+100+i*42},10,i==0?GREEN:TEXT,i==0);
        section(d,{a.left+220,a.top,a.right-250,a.bottom},L"ASSETS",L"Grid / List · Search assets");
        const wchar_t* assets[]={L"Intro_Video_4K_60.mp4",L"Sponsor_Ad_UGC_Main.mov",L"Twitch_LowerThird_Blue.png",L"Live_BGM_Loop_Synth.wav",L"Hype_SFX_Cheer.wav"};
        const wchar_t* info[]={L"30 sec · 142.4 MB",L"15 sec · 85.1 MB",L"4.2 MB",L"4:12 · 32.0 MB",L"4 sec · 1.8 MB"};
        for(int i=0;i<5;i++){int y=a.top+65+i*62;box(d,{a.left+234,y,a.right-266,y+50},i==0?RGB(25,50,42):CARD2,i==0?GREEN:BORDER);text(d,assets[i],{a.left+248,y+3,a.right-280,y+25},10,TEXT,i==0);text(d,info[i],{a.left+248,y+25,a.right-280,y+45},9,MUTED);}
        section(d,{a.right-235,a.top,a.right,a.bottom},L"INSPECTOR",L"Selected asset");
        text(d,c.selected_asset.c_str(),{a.right-220,a.top+70,a.right-15,a.top+110},10,TEXT,true);
        row(d,{a.right-220,a.top+125,a.right-15,a.top+158},L"Codec",L"MPEG-4");
        row(d,{a.right-220,a.top+158,a.right-15,a.top+191},L"Video",L"45 Mbps VBR");
        row(d,{a.right-220,a.top+191,a.right-15,a.top+224},L"Frame rate",L"60 FPS");
        button(d,{a.right-220,a.top+250,a.right-15,a.top+288},L"INSERT INTO SCENE",true);
        button(d,{a.right-220,a.top+298,a.right-15,a.top+336},L"RE-ENCODE H.265");
    }
    static void paintAudio(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.right,a.top+78},L"PROFESSIONAL MULTI-CHANNEL AUDIO DECK",L"Monitor · Headphones (Realtek High Def) · Volume -12.0 dB · A/V Sync +12 ms");
        const wchar_t* names[]={L"Microphone",L"Desktop",L"Camera",L"WebRTC REM"};
        for(int i=0;i<4;i++){int y=a.top+94+i*82;box(d,{a.left+8,y,a.left+350,y+70},CARD2,BORDER);text(d,names[i],{a.left+20,y+8,a.left+150,y+30},10,TEXT,true);text(d,(c.audio_muted[i]?L"MUTED":L"-18 dBFS"),{a.left+20,y+34,a.left+120,y+56},9,c.audio_muted[i]?RED:GREEN,true);pill(d,{a.left+165,y+10,a.left+215,y+32},c.audio_muted[i]?L"MUTE":L"MUTE",c.audio_muted[i]?RGB(77,24,31):BORDER,c.audio_muted[i]?RED:MUTED);pill(d,{a.left+222,y+10,a.left+272,y+32},c.audio_solo[i]?L"SOLO":L"SOLO",c.audio_solo[i]?RGB(28,67,94):BORDER,c.audio_solo[i]?BLUE:MUTED);fill(d,{a.left+165,y+46,a.left+330,y+53},BORDER);fill(d,{a.left+165,y+46,a.left+165+(165*c.audio_gain[i]/100),y+53},GREEN);}
        section(d,{a.right-310,a.top+94,a.right,a.bottom},L"OUTPUT ROUTER",L"Signal destinations");
        row(d,{a.right-292,a.top+160,a.right-18,a.top+193},L"MAIN MIX BUS",L"Stream Encoder 1");
        row(d,{a.right-292,a.top+193,a.right-18,a.top+226},L"MIC/AUX OVERLAY",L"Virtual Audio Cable");
        row(d,{a.right-292,a.top+226,a.right-18,a.top+259},L"MONITOR DECK",L"Realtek Phones");
        metric(d,a.right-292,a.top+300,130,L"LOOPBACK",L"2.4 ms",GREEN);
        metric(d,a.right-150,a.top+300,120,L"SYNC",L"+12 ms",GREEN);
    }
    static void paintWebRtc(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.left+250,a.bottom},L"SIGNALING SERVER",L"CONNECTED · 4 channels · total latency 15 ms");
        pill(d,{a.left+16,a.top+70,a.left+225,a.top+104},L"CONNECTED",RGB(21,77,55),GREEN);
        text(d,L"https://luma.live/room/sports-live",{a.left+16,a.top+130,a.left+232,a.top+175},9,TEXT);
        button(d,{a.left+16,a.top+190,a.left+225,a.top+228},L"COPY INVITE LINK",true);
        section(d,{a.left+265,a.top,a.right-245,a.bottom},L"REMOTE CALLERS",L"Live peer connections");
        const wchar_t* peers[]={L"John Doe · Host",L"Jane Smith · Remote Co-Host",L"Guest Speaker 3"};
        for(int i=0;i<3;i++){int y=a.top+68+i*88;box(d,{a.left+280,y,a.right-260,y+70},i==0?RGB(24,55,45):CARD2,i==0?GREEN:BORDER);text(d,peers[i],{a.left+294,y+8,a.right-275,y+30},10,TEXT,true);pill(d,{a.right-365,y+10,a.right-280,y+31},i==1?L"RECONNECTING":L"CONNECTED",i==1?RGB(74,55,23):RGB(21,77,55),i==1?AMBER:GREEN);text(d,L"RTT "+std::to_wstring(i==1?45:15)+L" ms · 2.8 Mbps",{a.left+294,y+38,a.right-275,y+60},9,MUTED);}
        section(d,{a.right-225,a.top,a.right,a.bottom},L"CONNECTION SPECS",L"ICE / DTLS / codecs");
        row(d,{a.right-208,a.top+70,a.right-16,a.top+103},L"ICE",L"STUN · srflx");
        row(d,{a.right-208,a.top+103,a.right-16,a.top+136},L"Protocol",L"UDP · DTLS/SRTP");
        row(d,{a.right-208,a.top+136,a.right-16,a.top+169},L"Video",L"VP8 · Profile 0");
        row(d,{a.right-208,a.top+169,a.right-16,a.top+202},L"Audio",L"Opus · 48 kHz");
        row(d,{a.right-208,a.top+202,a.right-16,a.top+235},L"Jitter buffer",L"4.5 ms");
    }
    static void paintLive(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.left+(a.right-a.left)*.66,a.top+285},L"PREVIEW / PROGRAM",L"Broadcast Station · 6200 kb/s");
        fill(d,{a.left+16,a.top+62,a.left+(a.right-a.left)*.33-8,a.top+260},RGB(27,34,41));fill(d,{a.left+(a.right-a.left)*.33+8,a.top+62,a.left+(a.right-a.left)*.66-16,a.top+260},RGB(30,38,44));
        section(d,{a.left+(a.right-a.left)*.69,a.top,a.right,a.bottom},L"BROADCAST STATION",L"RTMP endpoint · OAuth stream key");
        button(d,{a.left+(a.right-a.left)*.69+14,a.top+70,a.right-14,a.top+112},L"STOP BROADCAST",false,true);
        button(d,{a.left+(a.right-a.left)*.69+14,a.top+122,a.right-14,a.top+164},c.broadcast_paused?L"RESUME HARDWARE RECORDING":L"PAUSE HARDWARE RECORDING");
        text(d,c.oauth_locked?L"Endpoint changes restricted until idle":L"Endpoint editing unlocked",{a.left+(a.right-a.left)*.69+14,a.top+185,a.right-14,a.top+225},9,c.oauth_locked?AMBER:GREEN,true);
        metric(d,a.left+(a.right-a.left)*.69+14,a.top+245,120,L"DROPPED",L"0",GREEN);
        metric(d,a.left+(a.right-a.left)*.69+140,a.top+245,120,L"JITTER",L"1.2 ms",GREEN);
        metric(d,a.left+(a.right-a.left)*.69+14,a.top+305,120,L"VIDEO",L"5820 kbps");
        metric(d,a.left+(a.right-a.left)*.69+140,a.top+305,120,L"PING",L"8 ms");
        section(d,{a.left,a.top+300,a.left+(a.right-a.left)*.66,a.bottom},L"ENCODER INTEGRITY",L"NVIDIA NVENC · Low Latency Enabled");
        row(d,{a.left+14,a.top+360,a.left+(a.right-a.left)*.66-14,a.top+393},L"Keyframe interval",L"2 seconds");
        row(d,{a.left+14,a.top+393,a.left+(a.right-a.left)*.66-14,a.top+426},L"Multipass",L"Quarter Resolution");
    }
    static void paintRecordings(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.right,a.top+65},L"RECORDED SESSIONS",L"Search output files · All formats · MP4 only · Rebuild database");
        const wchar_t* files[]={L"rec_esports_final_ch1_main.mp4",L"tech_summit_keynote_backup.mp4",L"podcast_ep42_studio_iso.mp4",L"live_show_segment_render_01.mkv"};
        const wchar_t* sizes[]={L"12.4 GB",L"4.8 GB",L"28.1 GB",L"CONVERTING"};
        for(int i=0;i<4;i++){int y=a.top+78+i*62;box(d,{a.left+8,y,a.right-300,y+52},i==c.selected_recording?RGB(24,56,45):CARD2,i==c.selected_recording?GREEN:BORDER);text(d,files[i],{a.left+20,y+4,a.right-320,y+25},10,TEXT,i==c.selected_recording);text(d,sizes[i],{a.left+20,y+27,a.right-320,y+46},9,i==3?AMBER:MUTED);}
        section(d,{a.right-280,a.top+78,a.right,a.bottom},L"SESSION PREVIEW",L"Replay utilities");
        metric(d,a.right-264,a.top+145,115,L"RESOLUTION",L"3840×2160");
        metric(d,a.right-135,a.top+145,110,L"FPS",L"60");
        row(d,{a.right-264,a.top+220,a.right-14,a.top+253},L"Target bitrate",L"24,500 kbps");
        row(d,{a.right-264,a.top+253,a.right-14,a.top+286},L"Audio",L"AAC 48 kHz");
        button(d,{a.right-264,a.top+315,a.right-14,a.top+353},L"REVEAL IN EXPLORER");
        button(d,{a.right-264,a.top+363,a.right-14,a.top+401},L"EXPORT SEGMENT MP4",true);
    }
    static void paintDiagnostics(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.right,a.top+68},L"STREAMING TELEMETRY & PATH ANALYSIS",L"ONLINE / STABLE · Minor Jitter");
        button(d,{a.left+14,a.top+84,a.left+135,a.top+120},L"RESET STATS");
        button(d,{a.left+145,a.top+84,a.left+275,a.top+120},L"EXPORT REPORT");
        const wchar_t* labs[]={L"Video bitrate",L"Stream frame rate",L"Dropped frames",L"Latency jitter RTT"};
        const wchar_t* vals[]={L"6220 kb/s",L"59.8 FPS",L"13 · 0.01%",L"12 ms · max 45 ms"};
        for(int i=0;i<4;i++){int col=i%2,rowi=i/2;int x=a.left+14+col*220,y=a.top+145+rowi*125;section(d,{x,y,x+205,y+105},labs[i],nullptr);text(d,vals[i],{x+14,y+42,x+190,y+70},13,i==2?AMBER:GREEN,true);for(int k=0;k<8;k++)fill(d,{x+14+k*21,y+82-k%3*7,x+28+k*21,y+88-k%3*7},i==2?AMBER:GREEN);}
        section(d,{a.left+450,a.top+145,a.right-230,a.bottom},L"END-TO-END PATHS",L"RTMP connected · backup retrying");
        row(d,{a.left+466,a.top+210,a.right-244,a.top+243},L"Twitch primary",L"CONNECTED",GREEN);
        row(d,{a.left+466,a.top+243,a.right-244,a.top+276},L"YouTube backup",L"RETRYING",AMBER);
        row(d,{a.left+466,a.top+276,a.right-244,a.top+309},L"Backup RTT",L"45 ms",AMBER);
        section(d,{a.right-215,a.top+145,a.right,a.bottom},L"ENCODER",L"NVIDIA GeForce RTX 4070");
        row(d,{a.right-200,a.top+210,a.right-14,a.top+243},L"Preset",L"P6 Slower");
        row(d,{a.right-200,a.top+243,a.right-14,a.top+276},L"Multipass",L"Quarter Res");
        row(d,{a.right-200,a.top+276,a.right-14,a.top+309},L"Latency",L"LOW",GREEN);
        text(d,L"[INFO] encoder frame accepted",{a.right-200,a.top+340,a.right-14,a.top+365},8,MUTED);
        text(d,L"[WARN] backup path retry",{a.right-200,a.top+365,a.right-14,a.top+390},8,AMBER);
    }
    static void paintCopilot(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.left+330,a.bottom},L"LUMALIVE CO-PILOT",L"Broadcast orchestration · optimization advisory");
        pill(d,{a.left+16,a.top+68,a.left+145,a.top+101},c.copilot_auto?L"AUTO · ON":L"AUTO · OFF",c.copilot_auto?RGB(21,77,55):CARD2,c.copilot_auto?GREEN:MUTED);
        text(d,L"Observed runtime context",{a.left+16,a.top+125,a.left+310,a.top+150},10,MUTED,true);
        const wchar_t* ctx[]={L"Audio peak · -0.4 dBFS",L"WebRTC West RTT · 124 ms",L"RTMP backup · retrying",L"GPU · 48.3%",L"Disk free · 182 GB"};
        for(int i=0;i<5;i++)text(d,ctx[i],{a.left+18,a.top+160+i*35,a.left+305,a.top+188+i*35},10,TEXT);
        section(d,{a.left+350,a.top,a.right,a.bottom},L"AI RECOMMENDATIONS",L"Actions are explicit and auditable");
        const wchar_t* actions[]={L"Normalize MIC/AUX limiter to -1.0 dBTP",L"Switch Program to PIP CAM 1",L"Reinitialize WebRTC West connection"};
        for(int i=0;i<3;i++){int y=a.top+68+i*92;box(d,{a.left+366,y,a.right-16,y+76},CARD2,BORDER);text(d,actions[i],{a.left+380,y+10,a.right-180,y+35},10,TEXT,true);button(d,{a.right-160,y+18,a.right-32,y+52},L"EXECUTE",i==0);}
    }
    static void paintDevices(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.right,a.top+70},L"DEVICE ORCHESTRATION & HARDWARE CONTROL",L"Camera · Microphone · USB · AV Sync");
        const wchar_t* devs[]={L"Logitech Brio Pro · Camera",L"Sennheiser Profile USB · Microphone",L"Monitor 1 · Display Capture"};
        for(int i=0;i<3;i++){int y=a.top+88+i*92;box(d,{a.left+12,y,a.right-12,y+76},i==c.selected_device?RGB(25,55,45):CARD2,i==c.selected_device?GREEN:BORDER);text(d,devs[i],{a.left+28,y+12,a.right-250,y+38},11,TEXT,true);row(d,{a.left+28,y+42,a.right-260,y+70},i==0?L"Format":L"Status",i==0?L"4K · 60 FPS":L"CONNECTED",GREEN);}
        section(d,{a.right-235,a.top+88,a.right,a.bottom},L"HARDWARE DIAGNOSTICS",L"USB handshake · AV sync");
        row(d,{a.right-218,a.top+150,a.right-16,a.top+183},L"USB handshake",L"PASS",GREEN);
        row(d,{a.right-218,a.top+183,a.right-16,a.top+216},L"Noise suppression",L"ON",GREEN);
        row(d,{a.right-218,a.top+216,a.right-16,a.top+249},L"AV Sync",L"+12 ms",GREEN);
        button(d,{a.right-218,a.top+275,a.right-16,a.top+313},L"RUN DEVICE TEST",true);
    }
    static void paintSettings(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.left+205,a.bottom},L"CONFIGURATION",L"Studio Control Centre");
        const wchar_t* tabs[]={L"Canvas / Encoder",L"Audio Hardware",L"Encoder & Output",L"Network Routing",L"Hotkeys",L"Security & OAuth"};
        for(int i=0;i<6;i++)text(d,tabs[i],{a.left+16,a.top+68+i*44,a.left+190,a.top+96+i*44},10,i==0?GREEN:TEXT,i==0);
        section(d,{a.left+225,a.top,a.right,a.bottom},L"CANVAS / ENCODER",L"Current production profile");
        row(d,{a.left+242,a.top+75,a.right-16,a.top+108},L"Canvas",L"1920 × 1080");
        row(d,{a.left+242,a.top+108,a.right-16,a.top+141},L"Frame rate",L"60 FPS");
        row(d,{a.left+242,a.top+141,a.right-16,a.top+174},L"Encoder",L"NVIDIA NVENC");
        row(d,{a.left+242,a.top+174,a.right-16,a.top+207},L"Keyframe",L"2 seconds");
        row(d,{a.left+242,a.top+207,a.right-16,a.top+240},L"Low latency",L"ENABLED",GREEN);
        pill(d,{a.left+242,a.top+265,a.right-16,a.top+300},c.oauth_locked?L"ENDPOINT CHANGES LOCKED WHILE LIVE":L"ENDPOINT EDITING UNLOCKED",c.oauth_locked?RGB(72,52,22):RGB(21,77,55),c.oauth_locked?AMBER:GREEN);
    }
    static void paintSetup(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.right,a.top+70},L"INITIALIZATION WIZARD",L"Project Template · Canvas Specs · Hardware Detect · Output Router · Final Dry-run");
        const wchar_t* steps[]={L"Project Template",L"Canvas Specs",L"Hardware Detect",L"Output Router",L"Final Dry-run"};
        for(int i=0;i<5;i++){int x=a.left+14+i*190;box(d,{x,a.top+92,x+172,a.top+138},i==c.setup_step?RGB(21,77,55):CARD2,i==c.setup_step?GREEN:BORDER);text(d,steps[i],{x+6,a.top+92,x+166,a.top+138},9,i==c.setup_step?GREEN:TEXT,i==c.setup_step,true);}
        section(d,{a.left,a.top+165,a.left+(a.right-a.left)*.65,a.bottom},L"SETUP PROFILE",L"Broadcast project initialization");
        row(d,{a.left+16,a.top+225,a.left+(a.right-a.left)*.65-16,a.top+258},L"Project",c.project.c_str());
        row(d,{a.left+16,a.top+258,a.left+(a.right-a.left)*.65-16,a.top+291},L"Output",L"RTMP · OAuth");
        row(d,{a.left+16,a.top+291,a.left+(a.right-a.left)*.65-16,a.top+324},L"Encoder",L"NVENC");
        row(d,{a.left+16,a.top+324,a.left+(a.right-a.left)*.65-16,a.top+357},L"Dry-run",L"READY",GREEN);
        section(d,{a.left+(a.right-a.left)*.68,a.top+165,a.right,a.bottom},L"STEP ACTIONS",L"Use Next to advance");
        button(d,{a.left+(a.right-a.left)*.68+16,a.top+225,a.right-16,a.top+265},L"PREVIOUS");
        button(d,{a.left+(a.right-a.left)*.68+16,a.top+275,a.right-16,a.top+315},L"NEXT",true);
        button(d,{a.left+(a.right-a.left)*.68+16,a.top+325,a.right-16,a.top+365},L"RUN DRY-RUN");
    }
    static void paintAlerts(HDC d,const RECT& a,StudioPageContext& c){
        section(d,{a.left,a.top,a.right,a.top+70},L"ALERTS & RECOVERY",L"Operational recovery actions · "+std::to_wstring(c.alert_count)+L" active alerts");
        const wchar_t* titles[]={L"RTMP disconnect · automatic reconnect active",L"Camera USB bus reset required",L"GOP buffer nearing capacity",L"Disk space below 200 GB threshold"};
        const wchar_t* actions[]={L"BACKUP NODE",L"USB BUS RESET",L"FLUSH GOP BUFFER",L"OPEN STORAGE"};
        for(int i=0;i<4;i++){int y=a.top+88+i*78;box(d,{a.left+12,y,a.right-12,y+62},i==0?RGB(69,44,23):CARD2,i==0?AMBER:BORDER);text(d,titles[i],{a.left+26,y+8,a.right-210,y+34},10,TEXT,true);button(d,{a.right-190,y+13,a.right-28,y+47},actions[i],i==0);}
        section(d,{a.left,a.top+420,a.right,a.bottom},L"RECOVERY LOGS",L"Stream outage · path jitter");
        text(d,L"[INFO] reconnect attempt 3/5",{a.left+18,a.top+472,a.right-18,a.top+495},9,MUTED);
        text(d,L"[WARN] backup path RTT 45 ms",{a.left+18,a.top+498,a.right-18,a.top+521},9,AMBER);
        text(d,L"[INFO] recovery policy remains active",{a.left+18,a.top+524,a.right-18,a.top+547},9,GREEN);
    }
};

}

StudioPageRouter::StudioPageRouter(){
 navigation_={StudioPage::Main,StudioPage::Compact,StudioPage::Scenes,StudioPage::Inspector,StudioPage::Media,StudioPage::Audio,StudioPage::WebRtc,StudioPage::Live,StudioPage::Recordings,StudioPage::Diagnostics,StudioPage::Copilot,StudioPage::Devices,StudioPage::Settings,StudioPage::Setup,StudioPage::Alerts};
 const auto add=[this](StudioPage p,const wchar_t* t,const wchar_t* s){pages_.push_back(std::make_unique<Page>(p,t,s));};
 add(StudioPage::Main,L"Main Workspace",L"Preview · Program · Scenes · Sources · Mixer · Encoder");
 add(StudioPage::Compact,L"Compact Live Deck",L"Preview · Program · Quick Controls · Mixer");
 add(StudioPage::Scenes,L"Scene Editor",L"Layers · Snap Grid · Safe Areas · Interactive Canvas");
 add(StudioPage::Inspector,L"Source Inspector",L"Transform · Audio Routing · Effects");
 add(StudioPage::Media,L"Media Library",L"Video · Audio · Graphics · Live Captures & Feeds");
 add(StudioPage::Audio,L"Professional Audio Deck",L"Monitoring · Channels · Output Router");
 add(StudioPage::WebRtc,L"WebRTC Live Peers",L"Signaling · Remote Callers · ICE · DTLS/SRTP");
 add(StudioPage::Live,L"Live Streaming Control",L"Broadcast Station · OAuth · Encoder Integrity");
 add(StudioPage::Recordings,L"Recording Catalog",L"Sessions · Replay · Export · File Utilities");
 add(StudioPage::Diagnostics,L"Stream Diagnostics",L"Telemetry · Path Analysis · Encoder Logs");
 add(StudioPage::Copilot,L"LumaLive Co-Pilot",L"Broadcast orchestration · optimization advisory · executable actions");
 add(StudioPage::Devices,L"Device Orchestration",L"Camera · Microphone · USB · AV Sync");
 add(StudioPage::Settings,L"Configuration Control Centre",L"Canvas · Audio Hardware · Encoder · Network · OAuth");
 add(StudioPage::Setup,L"Initialization Wizard",L"Project Template · Canvas · Hardware · Output Router · Dry-run");
 add(StudioPage::Alerts,L"Alerts & Recovery",L"Stream outage · path jitter · recovery actions");
}
void StudioPageRouter::navigate(StudioPage p) noexcept{if(std::find(navigation_.begin(),navigation_.end(),p)!=navigation_.end())current_=p;}
const IStudioPage& StudioPageRouter::page() const noexcept{for(const auto& p:pages_)if(p->id()==current_)return *p;return *pages_.front();}
IStudioPage& StudioPageRouter::page() noexcept{for(auto& p:pages_)if(p->id()==current_)return *p;return *pages_.front();}
const wchar_t* StudioPageRouter::label(StudioPage p) noexcept{
 switch(p){
 case StudioPage::Main:return L"MAIN";case StudioPage::Compact:return L"COMPACT";case StudioPage::Scenes:return L"SCENES";case StudioPage::Inspector:return L"INSPECT";
 case StudioPage::Media:return L"MEDIA";case StudioPage::Audio:return L"AUDIO";case StudioPage::WebRtc:return L"WEBRTC";case StudioPage::Live:return L"LIVE";
 case StudioPage::Recordings:return L"RECORD";case StudioPage::Diagnostics:return L"DIAG";case StudioPage::Copilot:return L"AI";case StudioPage::Devices:return L"DEVICES";
 case StudioPage::Settings:return L"CONFIG";case StudioPage::Setup:return L"SETUP";case StudioPage::Alerts:return L"ALERTS";
 }return L"MAIN";
}
}
