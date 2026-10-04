#pragma once
#include "UiLocale.hpp"
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
namespace luma::client::ui::preview::UiTheme {
inline constexpr COLORREF Background=RGB(8,10,13),Surface=RGB(16,19,24),Elevated=RGB(24,29,36),Border=RGB(39,45,55),Text=RGB(232,237,245),Muted=RGB(145,155,171),Accent=RGB(0,145,245),AccentSoft=RGB(20,48,70),Danger=RGB(238,100,110);
inline HFONT Font(int size,int weight=400){return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ChineseUi()?L"Microsoft YaHei UI":L"Segoe UI");}
inline void Fill(HDC dc,RECT r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
inline constexpr UINT_PTR HoverTimer=0x4c554d41;
inline bool AnimationsEnabled(){BOOL enabled=FALSE;return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0)&&enabled;}
inline int HoverAmount(HWND h){return int(reinterpret_cast<INT_PTR>(GetPropW(h,L"LumaHoverAmount")));}
inline void SetHoverAmount(HWND h,int value){SetPropW(h,L"LumaHoverAmount",reinterpret_cast<HANDLE>(INT_PTR(value)));InvalidateRect(h,nullptr,FALSE);}
inline void SetHovered(HWND h,bool hovered){
 if(hovered)SetPropW(h,L"LumaHover",HANDLE(1));else RemovePropW(h,L"LumaHover");
 const int target=hovered?255:0;
 if(!AnimationsEnabled()||!IsWindowVisible(h)||!IsWindowEnabled(h)||!SetTimer(h,HoverTimer,16,nullptr)){KillTimer(h,HoverTimer);SetHoverAmount(h,target);}
}
inline COLORREF Blend(COLORREF from,COLORREF to,int amount){auto channel=[&](int a,int b){return (a*(255-amount)+b*amount)/255;};return RGB(channel(GetRValue(from),GetRValue(to)),channel(GetGValue(from),GetGValue(to)),channel(GetBValue(from),GetBValue(to)));}
inline LRESULT CALLBACK HoverProc(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
 // Owner-drawn buttons need Enter forwarded through dialog navigation.
 if(message==WM_GETDLGCODE&&l&&reinterpret_cast<MSG*>(l)->message==WM_KEYDOWN&&reinterpret_cast<MSG*>(l)->wParam==VK_RETURN)return DefSubclassProc(h,message,w,l)|DLGC_WANTMESSAGE;
 if(message==WM_KEYDOWN&&w==VK_RETURN){if(IsWindowEnabled(h)&&!(l&(LPARAM(1)<<30)))SendMessageW(h,BM_CLICK,0,0);return 0;}
 if(message==WM_MOUSEMOVE&&!GetPropW(h,L"LumaHover")){SetHovered(h,true);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}
 if(message==WM_MOUSELEAVE)SetHovered(h,false);
 if(message==WM_TIMER&&w==HoverTimer){const int target=GetPropW(h,L"LumaHover")?255:0;const int previous=HoverAmount(h);const int next=!AnimationsEnabled()?target:previous+std::clamp(target-previous,-48,48);SetHoverAmount(h,next);if(next==target)KillTimer(h,HoverTimer);return 0;}
 if(message==WM_SHOWWINDOW&&!w){KillTimer(h,HoverTimer);RemovePropW(h,L"LumaHover");SetHoverAmount(h,0);}
 if(message==WM_ENABLE&&!w){KillTimer(h,HoverTimer);RemovePropW(h,L"LumaHover");SetHoverAmount(h,0);}
 if(message==WM_NCDESTROY){KillTimer(h,HoverTimer);RemovePropW(h,L"LumaHoverAmount");RemovePropW(h,L"LumaHover");RemoveWindowSubclass(h,HoverProc,id);}
 return DefSubclassProc(h,message,w,l);
}
inline void InstallHover(HWND h){SetWindowSubclass(h,HoverProc,1,0);}

// Keep the native ComboBox selection, popup, type-ahead and accessibility tree.
// Only its closed field is painted here; parent list painting remains intact.
inline constexpr int ComboHeight=34;
inline UINT ComboDpi(HWND h){const auto stored=reinterpret_cast<UINT_PTR>(GetPropW(h,L"LumaComboDpi"));return stored?UINT(stored):GetDpiForWindow(h);}
inline void SetComboDpi(HWND h,UINT dpi){SetPropW(h,L"LumaComboDpi",reinterpret_cast<HANDLE>(UINT_PTR(dpi)));SendMessageW(h,CB_SETITEMHEIGHT,WPARAM(-1),MulDiv(ComboHeight,int(dpi),96));InvalidateRect(h,nullptr,FALSE);}
inline void DrawCombo(HWND h,HDC dc){
 const int saved=SaveDC(dc);RECT r{};GetClientRect(h,&r);const auto dpi=ComboDpi(h);auto px=[&](int value){return MulDiv(value,int(dpi),96);};
 const bool enabled=IsWindowEnabled(h),focused=GetFocus()==h||SendMessageW(h,CB_GETDROPPEDSTATE,0,0);
 Fill(dc,r,Surface);auto pen=CreatePen(PS_SOLID,std::max(1,px(1)),focused?Accent:Border);SelectObject(dc,pen);SelectObject(dc,GetStockObject(NULL_BRUSH));Rectangle(dc,0,0,r.right,r.bottom);
 auto font=reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0));if(font)SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,enabled?Text:Muted);
 const int length=GetWindowTextLengthW(h);std::wstring label(size_t(length)+1,L'\0');GetWindowTextW(h,label.data(),length+1);RECT content{px(12),0,r.right-px(34),r.bottom};DrawTextW(dc,label.c_str(),-1,&content,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
 auto arrow=CreatePen(PS_SOLID,std::max(1,px(2)),enabled?Muted:Border);SelectObject(dc,arrow);const int x=r.right-px(17),y=r.bottom/2;const int direction=SendMessageW(h,CB_GETDROPPEDSTATE,0,0)?-1:1;MoveToEx(dc,x-px(4),y-direction*px(2),nullptr);LineTo(dc,x,y+direction*px(2));LineTo(dc,x+px(4),y-direction*px(2));
 RestoreDC(dc,saved);DeleteObject(arrow);DeleteObject(pen);
}
inline LRESULT CALLBACK ComboProc(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
 if(message==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);DrawCombo(h,dc);EndPaint(h,&ps);return 0;}
 if(message==WM_PRINTCLIENT){DrawCombo(h,reinterpret_cast<HDC>(w));return 0;}
 if(message==WM_PRINT){if(l&PRF_CLIENT)DrawCombo(h,reinterpret_cast<HDC>(w));return 0;}
 if(message==WM_ERASEBKGND)return 1;
 if(message==WM_NCDESTROY){RemovePropW(h,L"LumaComboDpi");RemoveWindowSubclass(h,ComboProc,id);return DefSubclassProc(h,message,w,l);}
 const auto result=DefSubclassProc(h,message,w,l);
 if(message==WM_SETFONT||message==WM_DPICHANGED_AFTERPARENT)SetComboDpi(h,GetDpiForWindow(h));
 if(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_ENABLE||message==WM_SETFONT||message==CB_SETCURSEL||message==CB_SHOWDROPDOWN||message==WM_KEYDOWN||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)InvalidateRect(h,nullptr,FALSE);
 return result;
}
inline void InstallCombo(HWND h){SetWindowSubclass(h,ComboProc,2,0);SetComboDpi(h,GetDpiForWindow(h));}

inline void DrawButton(const DRAWITEMSTRUCT& d,HFONT font,bool primary=false,bool selected=false,bool danger=false){
 const int saved=SaveDC(d.hDC);const bool disabled=d.itemState&ODS_DISABLED;
 COLORREF fill=disabled?Surface:(selected?AccentSoft:(primary?Accent:Elevated));
 if(!disabled)fill=Blend(fill,primary?RGB(30,161,250):RGB(33,46,62),HoverAmount(d.hwndItem));
 if(!disabled&&(d.itemState&ODS_SELECTED))fill=AccentSoft;
 Fill(d.hDC,d.rcItem,Background);auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,1,selected?Accent:fill);SelectObject(d.hDC,brush);SelectObject(d.hDC,pen);
 const int radius=MulDiv(6,GetDpiForWindow(d.hwndItem),96);RoundRect(d.hDC,d.rcItem.left,d.rcItem.top,d.rcItem.right,d.rcItem.bottom,radius,radius);
 SelectObject(d.hDC,font);SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,disabled?RGB(88,101,117):(danger?Danger:Text));wchar_t label[256]{};GetWindowTextW(d.hwndItem,label,256);RECT text=d.rcItem;InflateRect(&text,-8,-2);DrawTextW(d.hDC,label,-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
 if(d.itemState&ODS_FOCUS){RECT focus=d.rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(d.hDC,&focus);}
 RestoreDC(d.hDC,saved);DeleteObject(brush);DeleteObject(pen);
}
}
