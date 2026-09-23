#include "StudioShell.hpp"
#include "StudioPageRouter.hpp"
#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwchar>

namespace luma::client::ui::studio {
namespace {
constexpr COLORREF BG=RGB(8,9,12),PANEL=RGB(13,16,20),BORDER=RGB(37,42,50);
constexpr COLORREF TEXT=RGB(231,237,242),MUTED=RGB(126,137,149),GREEN=RGB(53,217,145),RED=RGB(232,17,35),AMBER=RGB(236,171,72);

void fill(HDC dc,RECT r,COLORREF c){auto b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
void text(HDC dc,const wchar_t* s,RECT r,int size,COLORREF c,bool bold=false,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){
 auto f=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 auto o=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);DrawTextW(dc,s,-1,&r,flags);SelectObject(dc,o);DeleteObject(f);
}
void outline(HDC dc,RECT r,COLORREF c){auto p=CreatePen(PS_SOLID,1,c);auto o=SelectObject(dc,p);auto b=static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));auto ob=SelectObject(dc,b);Rectangle(dc,r.left,r.top,r.right,r.bottom);SelectObject(dc,ob);SelectObject(dc,o);DeleteObject(p);}
void button(HDC dc,RECT r,const wchar_t* s,bool active=false,bool danger=false){fill(dc,r,danger?RGB(67,22,28):(active?RGB(21,63,44):RGB(23,27,33)));outline(dc,r,danger?RGB(125,40,48):(active?GREEN:RGB(48,55,65)));text(dc,s,r,9,danger?RGB(245,180,186):TEXT,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
class Window {
 HWND h_{};HINSTANCE instance_{};StudioPageRouter router_;StudioPageContext ctx_;
 void paint(HDC dc){RECT rc;GetClientRect(h_,&rc);fill(dc,rc,BG);top(dc,rc);left(dc,rc);right(dc,rc);center(dc,rc);bottom(dc,rc);}
 void top(HDC dc,const RECT& rc){
   fill(dc,{0,0,rc.right,72},PANEL);outline(dc,{0,71,rc.right,72},BORDER);
   text(dc,L"LUMA",{18,12,80,42},21,TEXT,true);text(dc,L"LIVE",{80,12,136,42},21,GREEN,true);
   text(dc,ctx_.project.c_str(),{150,10,420,31},12,TEXT,true);text(dc,L"1920×1080 · 60.00 FPS",{150,34,420,55},10,MUTED);
   int x=425;const int rightBadges=150;for(auto p:router_.navigation()){const auto* lab=StudioPageRouter::label(p);int w=std::max(44,static_cast<int>(wcslen(lab))*7+16);if(x+w>rc.right-rightBadges)break;fill(dc,{x,13,x+w,42},p==router_.current()?RGB(21,43,35):PANEL);text(dc,lab,{x+5,14,x+w-5,41},8,p==router_.current()?GREEN:MUTED,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);x+=w+3;}
   fill(dc,{rc.right-138,13,rc.right-89,42},ctx_.live?RED:RGB(63,66,70));text(dc,ctx_.live?L"LIVE":L"OFF",{rc.right-138,13,rc.right-89,42},9,TEXT,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
   fill(dc,{rc.right-84,13,rc.right-35,42},ctx_.recording?RGB(125,32,41):RGB(63,66,70));text(dc,ctx_.recording?L"REC":L"IDLE",{rc.right-84,13,rc.right-35,42},9,TEXT,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 }
 void left(HDC dc,const RECT& rc){
   int w=220;fill(dc,{0,72,w,rc.bottom-44},PANEL);
   text(dc,L"SCENES",{16,87,w-16,110},10,MUTED,true);
   const wchar_t* s[]={L"Scene 1 · Host Intro",L"Scene 2 · Screen Share + Cam",L"Scene 3 · Full Camera 4K",L"Scene 4 · BRB Overlay"};
   for(int i=0;i<4;i++){int y=116+i*38;fill(dc,{10,y,w-10,y+30},(i==0?RGB(17,42,32):PANEL));text(dc,s[i],{18,y,w-18,y+30},9,i==0?GREEN:TEXT,i==0);}
   text(dc,L"SOURCES",{16,282,w-16,305},10,MUTED,true);
   const wchar_t* src[]={L"● Webcam Main · Logitech",L"● System Sound Router",L"● OBS Virtual Output"};
   for(int i=0;i<3;i++)text(dc,src[i],{18,310+i*30,w-18,336+i*30},9,TEXT);
   text(dc,L"WORKSPACE",{16,414,w-16,437},10,MUTED,true);
   button(dc,{12,445,w-12,480},L"+ ADD SOURCE",true);
   button(dc,{12,488,w-12,523},L"SCENE EDITOR");
   button(dc,{12,531,w-12,566},L"MEDIA LIBRARY");
   text(dc,L"AI ADVISORY",{16,590,w-16,613},10,MUTED,true);
   text(dc,L"Backup ingest unstable.",{18,622,w-18,645},9,AMBER,true);
   text(dc,L"WebRTC West RTT > 120 ms.",{18,647,w-18,670},9,AMBER);
 }
 void right(HDC dc,const RECT& rc){
   int x=rc.right-285;fill(dc,{x,72,rc.right,rc.bottom-44},PANEL);
   text(dc,L"SOURCE INSPECTOR",{x+16,88,rc.right-16,111},10,MUTED,true);
   const wchar_t* kv[]={L"Logitech Brio Pro Video",L"1920×1080 (16:9)",L"Transform 120 / 80 px",L"Opacity / Scale 100%",L"Chroma Key 34%",L"Hardware Acceleration ON"};
   for(int i=0;i<6;i++)text(dc,kv[i],{x+16,122+i*30,rc.right-16,148+i*30},9,i==5?GREEN:TEXT,i==5);
   text(dc,L"OUTPUT ROUTER",{x+16,315,rc.right-16,338},10,MUTED,true);
   const wchar_t* out[]={L"MAIN MIX BUS → RTMP 1",L"MIC/AUX → Virtual Cable",L"MONITOR → Realtek Phones"};
   for(int i=0;i<3;i++)text(dc,out[i],{x+16,347+i*29,rc.right-16,373+i*29},9,TEXT);
   text(dc,L"BROADCAST CONTROL",{x+16,445,rc.right-16,468},10,MUTED,true);
   button(dc,{x+16,478,rc.right-16,512},ctx_.live?L"STOP LIVE":L"START LIVE",false,ctx_.live);
   button(dc,{x+16,520,rc.right-16,554},ctx_.broadcast_paused?L"RESUME RECORD":L"PAUSE RECORDING");
   text(dc,L"CO-PILOT",{x+16,585,rc.right-16,608},10,MUTED,true);
   text(dc,L"Optimize audio · WebRTC · scenes",{x+16,616,rc.right-16,650},9,MUTED);
   button(dc,{x+16,662,rc.right-16,696},L"OPEN CO-PILOT",true);
 }
 void center(HDC dc,const RECT& rc){
   const int x=236,rx=rc.right-300,w=rx-x-16;int y=88;
   text(dc,router_.page().title(),{x,y,x+w,y+28},15,TEXT,true);text(dc,router_.page().subtitle(),{x,y+29,x+w,y+51},10,MUTED);
   RECT content{x,y+60,x+w,rc.bottom-54};router_.page().paint(dc,content,ctx_);
 }
 void bottom(HDC dc,const RECT& rc){
   int y=rc.bottom-44;fill(dc,{0,y,rc.right,rc.bottom},PANEL);outline(dc,{0,y,rc.right,y+1},BORDER);
   text(dc,L"STREAM:",{16,y+11,64,y+32},9,MUTED,true);fill(dc,{70,y+8,185,y+34},RGB(16,59,41));text(dc,ctx_.live?L"ONLINE · 6200 kbps":L"OFFLINE",{75,y+9,180,y+33},8,GREEN,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
   text(dc,L"CPU 24.1%",{205,y+11,290,y+32},9,TEXT);text(dc,L"GPU 48.3%",{305,y+11,390,y+32},9,TEXT);text(dc,L"MEM 4.1 / 16 GB",{405,y+11,520,y+32},9,TEXT);text(dc,L"RENDER 2.1 ms",{535,y+11,645,y+32},9,TEXT);text(dc,(L"WebRTC RTT "+std::to_wstring(ctx_.rtt_ms)+L" ms").c_str(),{660,y+11,790,y+32},9,TEXT);
   text(dc,ctx_.notice.c_str(),{810,y+11,rc.right-170,y+32},9,MUTED, false, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
   text(dc,L"v1.4.2",{rc.right-145,y+11,rc.right-18,y+32},9,MUTED,false,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
 }
 LRESULT handle(UINT m,WPARAM w,LPARAM l){
   switch(m){
   case WM_GETMINMAXINFO:{auto* mi=reinterpret_cast<MINMAXINFO*>(l);mi->ptMinTrackSize={1280,800};return 0;}
   case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(h_,&ps);paint(dc);EndPaint(h_,&ps);return 0;}
   case WM_ERASEBKGND:return 1;
   case WM_LBUTTONDOWN:{
     int x=GET_X_LPARAM(l),y=GET_Y_LPARAM(l);
     if(y<72){int nx=425;for(auto p:router_.navigation()){int ww=std::max(44,static_cast<int>(wcslen(StudioPageRouter::label(p)))*7+16);if(x>=nx&&x<nx+ww){router_.navigate(p);ctx_.notice=L"Page switched";InvalidateRect(h_,nullptr,FALSE);return 0;}nx+=ww+3;}}
     if(y>=72&&y<static_cast<int>(GetClientRectY())){RECT rc;GetClientRect(h_,&rc);RECT area{236,148,rc.right-300,rc.bottom-54};if(x>=area.left&&x<=area.right&&y>=area.top&&y<=area.bottom){if(router_.page().click(x,y,area,ctx_))InvalidateRect(h_,nullptr,FALSE);}}
     return 0;
   }
   case WM_KEYDOWN:
     if(w==VK_F11){ShowWindow(h_,IsZoomed(h_)?SW_RESTORE:SW_MAXIMIZE);return 0;}
     if(w==VK_ESCAPE){router_.navigate(StudioPage::Main);InvalidateRect(h_,nullptr,FALSE);return 0;}
     return 0;
   case WM_SIZE:InvalidateRect(h_,nullptr,FALSE);return 0;
   case WM_TIMER:InvalidateRect(h_,nullptr,FALSE);return 0;
   case WM_DESTROY:PostQuitMessage(0);return 0;
   }
   return DefWindowProcW(h_,m,w,l);
 }
 int GetClientRectY(){RECT r;GetClientRect(h_,&r);return r.bottom;}
 static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->h_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}return self?self->handle(m,w,l):DefWindowProcW(h,m,w,l);}
public:
 int run(HINSTANCE i,int show){
   instance_=i;WNDCLASSW wc{};wc.hInstance=i;wc.lpfnWndProc=proc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"LumaLiveStudioShell";RegisterClassW(&wc);
   h_=CreateWindowExW(0,wc.lpszClassName,L"LumaLive Studio — Design catalogue (sample data)",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,900,nullptr,nullptr,i,this);
   if(!h_)return 1;BOOL dark=TRUE;DwmSetWindowAttribute(h_,20,&dark,sizeof(dark));ShowWindow(h_,show);UpdateWindow(h_);SetTimer(h_,1,1000,nullptr);
   MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return static_cast<int>(msg.wParam);
 }
};
}
int RunStudioShell(HINSTANCE i,int show){Window w;return w.run(i,show);}
}
