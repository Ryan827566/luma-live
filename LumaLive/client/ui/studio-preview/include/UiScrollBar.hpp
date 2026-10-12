#pragma once
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <oleacc.h>
#include <algorithm>
#include <new>

namespace luma::client::ui::preview::UiScrollBar {
// Keep the EDIT's native range, wheel/keyboard behavior and accessibility object.
// Only its vertical nonclient bar is painted and pointer tracking is customized.
inline constexpr UINT_PTR SubclassId=0x4c554d53, RepeatTimer=0x4c554d54;
struct State {
 COLORREF surface,muted,thumb,accent;
 int hot=0,pressed=0,dragY=0,dragPosition=0,dragRange=0,dragTravel=0;
 POINT pointer{};
 bool repeating=false;
};
inline bool HighContrast(){HIGHCONTRASTW hc{sizeof(hc)};return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0)&&(hc.dwFlags&HCF_HIGHCONTRASTON);}
inline bool Info(HWND h,SCROLLBARINFO& bar){
 bar={sizeof(bar)};
 return !HighContrast()&&IsWindowEnabled(h)&&(GetWindowLongPtrW(h,GWL_STYLE)&WS_VSCROLL)&&
  GetScrollBarInfo(h,OBJID_VSCROLL,&bar)&&!(bar.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN));
}
inline int Part(const SCROLLBARINFO& b,POINT p){
 if(!PtInRect(&b.rcScrollBar,p))return 0;
 const int y=p.y-b.rcScrollBar.top,height=b.rcScrollBar.bottom-b.rcScrollBar.top;
 if(y<b.dxyLineButton)return 1;
 if(y>=height-b.dxyLineButton)return 5;
 if(y<b.xyThumbTop)return 2;
 if(y<b.xyThumbBottom)return 3;
 return 4;
}
inline void Fill(HDC dc,RECT r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
inline void Paint(HWND h,State& state,HDC target=nullptr){
 SCROLLBARINFO b{};if(!Info(h,b))return;
 RECT window{};GetWindowRect(h,&window);OffsetRect(&b.rcScrollBar,-window.left,-window.top);
 auto dc=target?target:GetWindowDC(h);if(!dc)return;const int saved=SaveDC(dc);
 const auto r=b.rcScrollBar;IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);Fill(dc,r,state.surface);
 const int scale=std::max(1,MulDiv(1,int(GetDpiForWindow(h)),96)),center=(r.left+r.right)/2;
 auto arrow=[&](int part,int y,bool down){
  const bool enabled=!(b.rgstate[part]&STATE_SYSTEM_UNAVAILABLE);
  auto pen=CreatePen(PS_SOLID,scale,enabled?((state.hot==part||state.pressed==part)?state.accent:state.muted):state.thumb);
  auto old=SelectObject(dc,pen);const int d=3*scale,sign=down?1:-1;
  MoveToEx(dc,center-d,y-sign*d/2,nullptr);LineTo(dc,center,y+sign*d/2);LineTo(dc,center+d,y-sign*d/2);
  SelectObject(dc,old);DeleteObject(pen);
 };
 arrow(1,r.top+b.dxyLineButton/2,false);arrow(5,r.bottom-b.dxyLineButton/2,true);
 if(b.xyThumbBottom>b.xyThumbTop&&!(b.rgstate[3]&STATE_SYSTEM_UNAVAILABLE)){
  RECT thumb{r.left+3*scale,r.top+b.xyThumbTop+scale,r.right-3*scale,r.top+b.xyThumbBottom-scale};
  auto brush=CreateSolidBrush(state.pressed==3?state.accent:state.hot==3?state.muted:state.thumb);
  auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
  RoundRect(dc,thumb.left,thumb.top,thumb.right,thumb.bottom,5*scale,5*scale);
  SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);
 }
 RestoreDC(dc,saved);if(!target)ReleaseDC(h,dc);
}
inline void End(HWND h,State& s){
 s.pressed=0;s.repeating=false;KillTimer(h,RepeatTimer);
 if(GetCapture()==h)ReleaseCapture();Paint(h,s);
}
inline void Step(HWND h,int part){
 const int command=part==1?SB_LINEUP:part==5?SB_LINEDOWN:part==2?SB_PAGEUP:SB_PAGEDOWN;
 SendMessageW(h,WM_VSCROLL,command,0);
}
inline LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
 auto& s=*reinterpret_cast<State*>(data);
 if(message==WM_NCDESTROY){KillTimer(h,RepeatTimer);RemoveWindowSubclass(h,Proc,id);delete &s;return DefSubclassProc(h,message,w,l);}
 if(message==WM_CANCELMODE||message==WM_CAPTURECHANGED||(message==WM_SHOWWINDOW&&!w)||message==WM_SETTINGCHANGE||(message==WM_ENABLE&&!w)){
  if(s.pressed)End(h,s);
 }
 SCROLLBARINFO b{};
 if((message==WM_NCLBUTTONDOWN||message==WM_NCLBUTTONDBLCLK)&&w==HTVSCROLL&&Info(h,b)){
  s.pointer={GET_X_LPARAM(l),GET_Y_LPARAM(l)};s.pressed=s.hot=Part(b,s.pointer);
  if(!s.pressed||(b.rgstate[s.pressed]&STATE_SYSTEM_UNAVAILABLE)){s.pressed=0;return 0;}
  SetCapture(h);
  if(s.pressed==3){
   SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};GetScrollInfo(h,SB_VERT,&info);
   s.dragY=s.pointer.y;s.dragPosition=info.nPos;s.dragRange=std::max(0,info.nMax-info.nMin-int(info.nPage)+1);
   s.dragTravel=std::max(1,int(b.rcScrollBar.bottom-b.rcScrollBar.top)-2*b.dxyLineButton-(b.xyThumbBottom-b.xyThumbTop));
  }else{Step(h,s.pressed);s.repeating=false;SetTimer(h,RepeatTimer,400,nullptr);}
  Paint(h,s);return 0;
 }
 if(message==WM_MOUSEMOVE&&s.pressed){
  s.pointer={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(h,&s.pointer);
  if(s.pressed==3){
   const auto delta=static_cast<long long>(s.pointer.y-s.dragY)*s.dragRange/s.dragTravel;
   const int target=int(std::clamp(static_cast<long long>(s.dragPosition)+delta,0LL,static_cast<long long>(s.dragRange)));
   const int first=int(SendMessageW(h,EM_GETFIRSTVISIBLELINE,0,0));
   SendMessageW(h,EM_LINESCROLL,0,target-first);
  }else if(Info(h,b))s.hot=Part(b,s.pointer);
  Paint(h,s);return 0;
 }
 if(message==WM_LBUTTONUP&&s.pressed){End(h,s);return 0;}
 if(message==WM_TIMER&&w==RepeatTimer){
  if(!s.pressed||GetCapture()!=h||!Info(h,b)){End(h,s);return 0;}
  if(!s.repeating){s.repeating=true;SetTimer(h,RepeatTimer,60,nullptr);}
  if(Part(b,s.pointer)==s.pressed)Step(h,s.pressed);
  Paint(h,s);return 0;
 }
 if(message==WM_NCMOUSEMOVE&&w==HTVSCROLL&&Info(h,b)){
  s.hot=Part(b,{GET_X_LPARAM(l),GET_Y_LPARAM(l)});
  TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE|TME_NONCLIENT,h,0};TrackMouseEvent(&track);
 }
 if(message==WM_NCMOUSELEAVE)s.hot=0;
 const auto result=DefSubclassProc(h,message,w,l);
 switch(message){
  case WM_NCPAINT:case WM_PAINT:case WM_NCMOUSEMOVE:case WM_NCMOUSELEAVE:
  case WM_VSCROLL:case WM_MOUSEWHEEL:case EM_LINESCROLL:case EM_SCROLL:case EM_SCROLLCARET:
  case WM_SETTEXT:case WM_SIZE:case WM_SETFONT:case WM_KEYDOWN:case WM_KEYUP:
  case WM_THEMECHANGED:case WM_SETTINGCHANGE:Paint(h,s);break;
  case WM_PRINT:if(l&PRF_NONCLIENT)Paint(h,s,reinterpret_cast<HDC>(w));break;
 }
 return result;
}
inline bool Install(HWND h,COLORREF surface,COLORREF muted,COLORREF thumb,COLORREF accent){
 DWORD_PTR existing{};if(GetWindowSubclass(h,Proc,SubclassId,&existing))return true;
 auto state=new(std::nothrow) State{surface,muted,thumb,accent};if(!state)return false;
 if(!SetWindowSubclass(h,Proc,SubclassId,reinterpret_cast<DWORD_PTR>(state))){delete state;return false;}
 return true;
}
}
