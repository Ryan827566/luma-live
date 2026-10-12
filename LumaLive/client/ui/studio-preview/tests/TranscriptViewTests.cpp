#include "TranscriptView.hpp"
#include "UiScrollBar.hpp"
#include <iostream>
#include <string>
#include <stdexcept>
using namespace luma::client::ui::preview;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
LRESULT CALLBACK ClosingParent(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND&&HIWORD(w)==EN_VSCROLL&&GetPropW(h,L"CloseOnScroll")){
        RemovePropW(h,L"CloseOnScroll");DestroyWindow(reinterpret_cast<HWND>(l));return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
int main() {
    auto parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 600, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    auto edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 400, 180, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!parent || !edit) return 1;
    require(UiScrollBar::Install(edit,RGB(16,19,24),RGB(145,155,171),RGB(65,79,99),RGB(0,145,245)),"Scrollbar subclass failed");
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    auto first = [&] { return int(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0)); };
    try {
        std::wstring text;
        for (int i = 0; i < 100; ++i) text += L"Transcript line " + std::to_wstring(i) + L"\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        require(first() > 0, "Initial long transcript did not follow tail");
        require(GetWindowLongW(edit, GWL_STYLE) & WS_VSCROLL, "Overflow scrollbar hidden");
        const int bottom = first();
        SendMessageW(edit, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA * 4), 0);
        const int reading = first();
        require(reading < bottom, "Wheel did not leave tail");
        text += L"New incoming caption\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        require(first() == reading, "Incoming caption interrupted wheel reading");
        // Selected text must survive a refresh, even with a nonzero scroll position.
        SendMessageW(edit, EM_SETSEL, 20, 40);
        SendMessageW(edit, EM_LINESCROLL, 0, 20 - first());
        const int selectedTop = first();
        text += L"Another caption\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        DWORD start = 0, end = 0;
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        require(start == 20 && end == 40, "Selection lost during refresh");
        require(first() == selectedTop, "Selection restore moved reading position");
        const int length = GetWindowTextLengthW(edit);
        SendMessageW(edit, EM_SETSEL, length, length);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        const int resumed = first();
        text += L"Follow tail again\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        require(first() > resumed, "Returning to tail did not resume following");
        ShowWindow(parent, SW_HIDE);
        for (int i = 0; i < 20; ++i) text += L"Caption while workspace is hidden\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        require(first() > resumed + 15, "Hidden workspace stopped following incoming captions");
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        require(first() > resumed + 15, "Revealing workspace lost the latest captions");
        // Pointer behavior uses the same native range and retains text selection.
        auto bar = [&] { SCROLLBARINFO b{sizeof(b)};require(GetScrollBarInfo(edit,OBJID_VSCROLL,&b),"Native scrollbar inaccessible");return b; };
        auto press = [&](POINT p) { SendMessageW(edit,WM_NCLBUTTONDOWN,HTVSCROLL,MAKELPARAM(p.x,p.y)); };
        auto release = [&] { SendMessageW(edit,WM_LBUTTONUP,0,0);require(GetCapture()!=edit,"Scrollbar retained capture after release"); };
        auto b=bar();const int beforeArrow=first();
        press({(b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.top+b.dxyLineButton/2});
        require(first()==beforeArrow-1,"Scrollbar up arrow did not move one line");release();
        SendMessageW(edit,EM_LINESCROLL,0,-first());b=bar();
        press({(b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.bottom-b.dxyLineButton-2});
        require(first()>1,"Scrollbar track did not page down");release();
        SendMessageW(edit,EM_SETSEL,20,40);b=bar();
        POINT thumb{(b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.top+(b.xyThumbTop+b.xyThumbBottom)/2};
        press(thumb);
        POINT destination{thumb.x,b.rcScrollBar.bottom+80};ScreenToClient(edit,&destination);
        SendMessageW(edit,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(destination.x,destination.y));
        const int dragged=first();require(dragged>80,"Scrollbar thumb did not drag toward end");
        SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        require(start==20&&end==40,"Dragging scrollbar changed text selection");release();
        b=bar();press({(b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.top+b.dxyLineButton/2});
        SendMessageW(edit,WM_CANCELMODE,0,0);require(GetCapture()!=edit,"Cancel did not release scrollbar capture");
        const int afterCancel=first();SendMessageW(edit,WM_TIMER,UiScrollBar::RepeatTimer,0);
        require(first()==afterCancel,"Canceled scrollbar continued to repeat");
        // System accessibility still sees the native scrollbar/range after custom drawing.
        SCROLLINFO native{sizeof(native),SIF_RANGE|SIF_PAGE|SIF_POS};
        require(GetScrollInfo(edit,SB_VERT,&native)&&native.nMax>100&&native.nPage>0,"Native scroll range lost");
        b=bar();require(!(b.rgstate[0]&STATE_SYSTEM_INVISIBLE),"Native scrollbar became inaccessible");
        ReplaceTranscriptText(edit, L"Short transcript");
        require(first() == 0, "Short transcript retained invalid viewport");
        require(!(GetWindowLongW(edit, GWL_STYLE) & WS_VSCROLL), "Short transcript retained scrollbar");
        std::wstring longRecord;longRecord.reserve(210000);
        for(int i=0;i<70000;++i)longRecord+=L"x\r\n";
        ReplaceTranscriptText(edit,longRecord.c_str());
        SendMessageW(edit,EM_LINESCROLL,0,-first());b=bar();
        thumb={(b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.top+(b.xyThumbTop+b.xyThumbBottom)/2};
        press(thumb);destination={thumb.x,b.rcScrollBar.bottom+80};ScreenToClient(edit,&destination);
        SendMessageW(edit,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(destination.x,destination.y));
        require(first()>65535,"Long-record thumb position was truncated to 16 bits");
        // The range can grow or disappear while a user holds the thumb.
        for(int i=0;i<1000;++i)longRecord+=L"new\r\n";
        ReplaceTranscriptText(edit,longRecord.c_str());
        SendMessageW(edit,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(destination.x,destination.y));
        require(first()>70900,"Held thumb retained a stale range after incoming text");
        ReplaceTranscriptText(edit,L"Now short");
        SendMessageW(edit,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(destination.x,destination.y));
        require(GetCapture()!=edit,"Disappearing scrollbar did not cancel tracking");
        ReplaceTranscriptText(edit,longRecord.c_str());
        b=bar();press({(b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.top+b.dxyLineButton/2});
        const int repeatStart=first();SendMessageW(edit,WM_TIMER,UiScrollBar::RepeatTimer,0);
        require(first()==repeatStart-1,"Held scrollbar arrow did not repeat");
        ShowWindow(edit,SW_HIDE);require(GetCapture()!=edit,"Hidden scrollbar retained capture");
        auto list=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,0,0,220,180,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(list&&UiScrollBar::Install(list,RGB(16,19,24),RGB(145,155,171),RGB(65,79,99),RGB(0,145,245),true),"Participant scrollbar installation failed");
        for(int i=0;i<100;++i){auto name=L"Participant "+std::to_wstring(i);SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
        SendMessageW(list,LB_SETCURSEL,42,0);SendMessageW(list,LB_SETTOPINDEX,30,0);
        b={sizeof(b)};require(GetScrollBarInfo(list,OBJID_VSCROLL,&b),"Participant scrollbar unavailable");
        SendMessageW(list,WM_NCLBUTTONDOWN,HTVSCROLL,MAKELPARAM((b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.top+b.dxyLineButton/2));
        SendMessageW(list,WM_LBUTTONUP,0,0);require(SendMessageW(list,LB_GETTOPINDEX,0,0)==29,"Participant arrow did not scroll one row");
        GetScrollBarInfo(list,OBJID_VSCROLL,&b);thumb={(b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.top+(b.xyThumbTop+b.xyThumbBottom)/2};
        SendMessageW(list,WM_NCLBUTTONDOWN,HTVSCROLL,MAKELPARAM(thumb.x,thumb.y));
        destination={thumb.x,b.rcScrollBar.bottom+80};ScreenToClient(list,&destination);
        SendMessageW(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(destination.x,destination.y));SendMessageW(list,WM_LBUTTONUP,0,0);
        require(SendMessageW(list,LB_GETTOPINDEX,0,0)>80,"Participant thumb did not scroll");
        require(SendMessageW(list,LB_GETCURSEL,0,0)==42,"Participant scroll changed selected member");
        SendMessageW(list,WM_KEYDOWN,VK_DOWN,0);require(SendMessageW(list,LB_GETCURSEL,0,0)==43,"Participant keyboard navigation changed");
        DestroyWindow(list);
        WNDCLASSW cls{};cls.lpfnWndProc=ClosingParent;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LumaScrollbarDestroyTest";
        RegisterClassW(&cls);
        auto closing=CreateWindowExW(0,cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,400,300,nullptr,nullptr,cls.hInstance,nullptr);
        auto transient=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY,0,0,250,100,closing,nullptr,cls.hInstance,nullptr);
        require(transient&&UiScrollBar::Install(transient,RGB(16,19,24),RGB(145,155,171),RGB(65,79,99),RGB(0,145,245)),"Transient scrollbar setup failed");
        ReplaceTranscriptText(transient,longRecord.c_str());ShowWindow(closing,SW_SHOWNOACTIVATE);
        b={sizeof(b)};require(GetScrollBarInfo(transient,OBJID_VSCROLL,&b),"Transient scrollbar unavailable");
        SetPropW(closing,L"CloseOnScroll",HANDLE(1));
        SendMessageW(transient,WM_NCLBUTTONDOWN,HTVSCROLL,MAKELPARAM((b.rcScrollBar.left+b.rcScrollBar.right)/2,b.rcScrollBar.top+b.dxyLineButton/2));
        require(!IsWindow(transient),"Parent did not destroy the EDIT during its scroll notification");
        require(GetCapture()!=transient,"Destroyed scrollbar retained capture");DestroyWindow(closing);
        std::cout << "PASS: native transcript wheel/selection/following and dark scrollbar arrows/page/drag/cancel/range\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n"; DestroyWindow(parent); return 1;
    }
    DestroyWindow(parent); return 0;
}
