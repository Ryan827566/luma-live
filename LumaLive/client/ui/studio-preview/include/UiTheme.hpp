#pragma once
#include "UiLocale.hpp"
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
namespace luma::client::ui::preview::UiTheme {
inline constexpr COLORREF Background=RGB(8,10,13),Surface=RGB(16,19,24),Elevated=RGB(24,29,36),Border=RGB(39,45,55),Text=RGB(232,237,245),Muted=RGB(145,155,171),Accent=RGB(0,145,245),AccentSoft=RGB(20,48,70),Danger=RGB(238,100,110);
inline HFONT Font(int size,int weight=400){return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ChineseUi()?L"Microsoft YaHei UI":L"Segoe UI");}
inline void Fill(HDC dc,RECT r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
inline LRESULT CALLBACK HoverProc(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
 // Owner-drawn buttons need Enter forwarded through dialog navigation.
 if(message==WM_GETDLGCODE&&l&&reinterpret_cast<MSG*>(l)->message==WM_KEYDOWN&&reinterpret_cast<MSG*>(l)->wParam==VK_RETURN)return DefSubclassProc(h,message,w,l)|DLGC_WANTMESSAGE;
 if(message==WM_KEYDOWN&&w==VK_RETURN){if(IsWindowEnabled(h)&&!(l&(LPARAM(1)<<30)))SendMessageW(h,BM_CLICK,0,0);return 0;}
 if(message==WM_MOUSEMOVE&&!GetPropW(h,L"LumaHover")){SetPropW(h,L"LumaHover",HANDLE(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}
 if(message==WM_MOUSELEAVE){RemovePropW(h,L"LumaHover");InvalidateRect(h,nullptr,FALSE);}
 if(message==WM_NCDESTROY){RemovePropW(h,L"LumaHover");RemoveWindowSubclass(h,HoverProc,id);}
 return DefSubclassProc(h,message,w,l);
}
inline void InstallHover(HWND h){SetWindowSubclass(h,HoverProc,1,0);}
inline void DrawButton(const DRAWITEMSTRUCT& d,HFONT font,bool primary=false,bool selected=false,bool danger=false){
 const int saved=SaveDC(d.hDC);const bool disabled=d.itemState&ODS_DISABLED;
 COLORREF fill=disabled?Surface:(selected?AccentSoft:(primary?Accent:Elevated));
 if(!disabled&&GetPropW(d.hwndItem,L"LumaHover"))fill=primary?RGB(30,161,250):RGB(33,46,62);
 if(!disabled&&(d.itemState&ODS_SELECTED))fill=AccentSoft;
 Fill(d.hDC,d.rcItem,Background);auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,1,selected?Accent:fill);SelectObject(d.hDC,brush);SelectObject(d.hDC,pen);
 const int radius=MulDiv(6,GetDpiForWindow(d.hwndItem),96);RoundRect(d.hDC,d.rcItem.left,d.rcItem.top,d.rcItem.right,d.rcItem.bottom,radius,radius);
 SelectObject(d.hDC,font);SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,disabled?RGB(88,101,117):(danger?Danger:Text));wchar_t label[256]{};GetWindowTextW(d.hwndItem,label,256);RECT text=d.rcItem;InflateRect(&text,-8,-2);DrawTextW(d.hDC,label,-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
 if(d.itemState&ODS_FOCUS){RECT focus=d.rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(d.hDC,&focus);}
 RestoreDC(d.hDC,saved);DeleteObject(brush);DeleteObject(pen);
}
}
