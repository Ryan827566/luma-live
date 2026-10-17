#pragma once
#include "UiTheme.hpp"
#include <windowsx.h>
#include <array>

namespace luma::client::ui::preview {
// Use the existing 40-DIP brand row as the titlebar on systems whose native
// caption cannot be colored. Native window styles retain system move/size/menu.
class WindowChrome {
public:
    static constexpr int Minimize=90,Maximize=91,Close=92;
    void Open(HWND owner){
        owner_=owner;
        const wchar_t* names[]={UiLabel(L"最小化",L"Minimize"),UiLabel(L"最大化",L"Maximize"),UiLabel(L"关闭窗口",L"Close window")};
        for(int i=0;i<3;++i){
            buttons_[i]=CreateWindowExW(0,L"BUTTON",names[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                0,0,1,1,owner,reinterpret_cast<HMENU>(INT_PTR(Minimize+i)),GetModuleHandleW(nullptr),nullptr);
            UiTheme::InstallHover(buttons_[i]);
        }
    }
    void Layout(UINT dpi,bool fullscreen){
        dpi_=dpi;fullscreen_=fullscreen;RECT bounds{};GetClientRect(owner_,&bounds);
        for(int i=0;i<3;++i){
            ShowWindow(buttons_[i],fullscreen?SW_HIDE:SW_SHOW);
            MoveWindow(buttons_[i],bounds.right-S((3-i)*46),S(5),S(46),S(30),TRUE);
        }
        SetWindowTextW(buttons_[1],IsZoomed(owner_)?UiLabel(L"还原窗口",L"Restore window"):UiLabel(L"最大化",L"Maximize"));
    }
    bool Command(int id){
        if(id<Minimize||id>Close)return false;
        PostMessageW(owner_,WM_SYSCOMMAND,id==Minimize?SC_MINIMIZE:id==Close?SC_CLOSE:IsZoomed(owner_)?SC_RESTORE:SC_MAXIMIZE,0);
        return true;
    }
    bool Draw(const DRAWITEMSTRUCT& item){
        if(item.CtlID<Minimize||item.CtlID>Close)return false;
        const auto dc=item.hDC;const auto rect=item.rcItem;
        const int state=SaveDC(dc);
        UiTheme::Fill(dc,rect,(item.itemState&ODS_SELECTED)?UiTheme::Border:UiTheme::Blend(UiTheme::Background,item.CtlID==Close?RGB(166,44,44):UiTheme::Elevated,UiTheme::HoverAmount(item.hwndItem)));
        const int cx=(rect.left+rect.right)/2,cy=(rect.top+rect.bottom)/2,r=S(5);
        auto pen=CreatePen(PS_SOLID,std::max(1,S(1)),UiTheme::Text);SelectObject(dc,pen);SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        if(item.CtlID==Minimize){MoveToEx(dc,cx-r,cy,nullptr);LineTo(dc,cx+r,cy);}
        else if(item.CtlID==Close){MoveToEx(dc,cx-r,cy-r,nullptr);LineTo(dc,cx+r,cy+r);MoveToEx(dc,cx+r,cy-r,nullptr);LineTo(dc,cx-r,cy+r);}
        else if(IsZoomed(owner_)){Rectangle(dc,cx-r+S(2),cy-r,cx+r,cy+r-S(2));UiTheme::Fill(dc,{cx-r,cy-r+S(2),cx+r-S(2),cy+r},UiTheme::Background);Rectangle(dc,cx-r,cy-r+S(2),cx+r-S(2),cy+r);}
        else Rectangle(dc,cx-r,cy-r,cx+r,cy+r);
        if(item.itemState&ODS_FOCUS){RECT focus=rect;InflateRect(&focus,-S(5),-S(4));DrawFocusRect(dc,&focus);}
        RestoreDC(dc,state);DeleteObject(pen);return true;
    }
    LRESULT HitTest(LPARAM position)const{
        POINT point{GET_X_LPARAM(position),GET_Y_LPARAM(position)};RECT window{};GetWindowRect(owner_,&window);
        if(fullscreen_)return HTCLIENT;
        const int edge=S(6);const bool left=point.x<window.left+edge,right=point.x>=window.right-edge;
        const bool top=point.y<window.top+edge,bottom=point.y>=window.bottom-edge;
        if(!IsZoomed(owner_)){
            if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;
            if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;
            if(top)return HTTOP;if(bottom)return HTBOTTOM;if(left)return HTLEFT;if(right)return HTRIGHT;
        }
        ScreenToClient(owner_,&point);
        if(point.y>=0&&point.y<S(40)){
            RECT client{};GetClientRect(owner_,&client);
            if(point.x<client.right-S(138))return HTCAPTION;
        }
        return HTCLIENT;
    }
private:
    int S(int value)const{return MulDiv(value,dpi_,96);}
    HWND owner_{};std::array<HWND,3> buttons_{};UINT dpi_=96;bool fullscreen_=false;
};
}
