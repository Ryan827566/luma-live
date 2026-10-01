#pragma once
#include "SessionAi.hpp"
#include "UiLocale.hpp"
#include <mmsystem.h>
namespace luma::client::ui::preview {
class AiPanel {
public:
 SessionAi session;
 ~AiPanel(){Close();}
 void Open(HWND owner,bool embedded=false){
  if(window_){ShowWindow(window_,SW_SHOW);if(!embedded_)SetForegroundWindow(window_);return;}
  embedded_=embedded;
  WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Proc;cls.lpszClassName=L"LumaSessionAi";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
  window_=CreateWindowExW(embedded_?WS_EX_CONTROLPARENT:0,cls.lpszClassName,UiLabel(L"AI \u5b57\u5e55\u4e0e\u4f1a\u8bae\u6458\u8981",L"AI captions and summaries"),embedded_?(WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS):WS_OVERLAPPEDWINDOW,embedded_?0:CW_USEDEFAULT,embedded_?0:CW_USEDEFAULT,720,560,owner,nullptr,cls.hInstance,this);
  if(!window_)return;
  font_=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Microsoft YaHei UI");
  auto add=[&](const wchar_t* type,const wchar_t* label,DWORD style,int id){auto h=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window_,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);return h;};
  note_=add(L"STATIC",UiLabel(L"\u9ed8\u8ba4\u5173\u95ed\u3002\u542f\u7528\u540e\uff0c\u672c\u6b21\u901a\u8bdd\u5404\u65b9\u8bed\u97f3\u5c06\u53d1\u9001\u81f3\u5df2\u914d\u7f6e\u7684 AI \u670d\u52a1\u3002\u8bf7\u544a\u77e5\u53c2\u4f1a\u8005\u3002\u5b57\u5e55\u7ea6\u6bcf 5 \u79d2\u66f4\u65b0\uff0c\u505c\u6b62\u540e\u4e22\u5f03\u672a\u5904\u7406\u7247\u6bb5\u3002",L"Off by default. Enabling sends participant audio to your configured AI service. Inform participants first. Captions update about every five seconds. Stopping discards pending segments."),0,1);
  toggle_=add(L"BUTTON",UiLabel(L"\u542f\u7528\u5b57\u5e55",L"Start captions"),WS_TABSTOP,2);summary_=add(L"BUTTON",UiLabel(L"\u751f\u6210\u6458\u8981\u4e0e\u5f85\u529e",L"Summary and actions"),WS_TABSTOP,3);
  language_=add(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,5);for(auto label:{UiLabel(L"\u82f1\u8bed",L"English"),UiLabel(L"\u4e2d\u6587",L"Chinese"),UiLabel(L"\u65e5\u8bed",L"Japanese"),UiLabel(L"\u97e9\u8bed",L"Korean"),UiLabel(L"\u897f\u73ed\u7259\u8bed",L"Spanish")})SendMessageW(language_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(language_,CB_SETCURSEL,0,0);
  translate_=add(L"BUTTON",UiLabel(L"\u7ffb\u8bd1\u8bb0\u5f55",L"Translate transcript"),WS_TABSTOP,6);
  speak_=add(L"BUTTON",UiLabel(L"AI \u6717\u8bfb\u6458\u8981",L"Read AI summary"),WS_TABSTOP,7);
  stopSpeech_=add(L"BUTTON",UiLabel(L"\u505c\u6b62\u6717\u8bfb",L"Stop reading"),WS_TABSTOP,8);
  keywords_=add(L"BUTTON",UiLabel(L"\u63d0\u53d6\u5173\u952e\u8bcd",L"Extract keywords"),WS_TABSTOP,9);
  text_=add(L"EDIT",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,4);
  SendMessageW(text_,EM_SETLIMITTEXT,200000,0);Layout();SetTimer(window_,1,300,nullptr);Refresh();ShowWindow(window_,SW_SHOW);
 }
 void Place(RECT r){if(window_)MoveWindow(window_,r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);}
 void Hide(){if(window_)ShowWindow(window_,SW_HIDE);}
 void Close(){session.Enable(false);if(!speech_.empty()){PlaySoundW(nullptr,nullptr,0);speech_.clear();}if(window_)DestroyWindow(window_);}
private:
 bool embedded_{false};
 HWND window_{},note_{},toggle_{},summary_{},text_{},language_{},translate_{},speak_{},stopSpeech_{},keywords_{};HFONT font_{};std::string shown_,speech_;
 static std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
 void Layout(){if(!text_)return;RECT r;GetClientRect(window_,&r);MoveWindow(note_,20,16,r.right-40,68,TRUE);MoveWindow(toggle_,20,90,140,34,TRUE);MoveWindow(summary_,172,90,180,34,TRUE);MoveWindow(language_,20,134,140,180,TRUE);MoveWindow(translate_,172,134,180,34,TRUE);MoveWindow(speak_,366,90,150,34,TRUE);MoveWindow(stopSpeech_,366,134,150,34,TRUE);MoveWindow(keywords_,20,178,180,34,TRUE);MoveWindow(text_,20,226,r.right-40,r.bottom-246,TRUE);}
 void Refresh(){auto voice=session.TakeSpeech();if(!voice.empty()){PlaySoundW(nullptr,nullptr,0);speech_=std::move(voice);PlaySoundW(reinterpret_cast<LPCWSTR>(speech_.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);}SetWindowTextW(toggle_,session.Enabled()?UiLabel(L"\u505c\u6b62\u5b57\u5e55",L"Stop captions"):UiLabel(L"\u542f\u7528\u5b57\u5e55",L"Start captions"));auto value=session.Text();if(value!=shown_){shown_=value;SetWindowTextW(text_,Wide(value).c_str());SendMessageW(text_,EM_SETSEL,WPARAM(-1),LPARAM(-1));SendMessageW(text_,EM_SCROLLCARET,0,0);}}
 static LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM w,LPARAM l){
  auto self=reinterpret_cast<AiPanel*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<AiPanel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(h,message,w,l);
  switch(message){
   case WM_SIZE:self->Layout();return 0;
   case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={560,420};return 0;
   case WM_TIMER:self->Refresh();return 0;
   case WM_COMMAND:if(LOWORD(w)==9){self->session.Keywords();self->Refresh();}else if(LOWORD(w)==7){self->session.Speak();}else if(LOWORD(w)==8){PlaySoundW(nullptr,nullptr,0);self->speech_.clear();}else if(LOWORD(w)==6){const char* langs[]={"en","zh","ja","ko","es"};auto index=SendMessageW(self->language_,CB_GETCURSEL,0,0);if(index>=0&&index<5)self->session.Translate(langs[index]);self->Refresh();}else if(LOWORD(w)==2){self->session.Enable(!self->session.Enabled());self->Refresh();}else if(LOWORD(w)==3){self->session.Summarize();self->Refresh();}return 0;
   case WM_CLOSE:self->Close();return 0;
   case WM_DESTROY:if(!self->speech_.empty()){PlaySoundW(nullptr,nullptr,0);self->speech_.clear();}KillTimer(h,1);self->window_=nullptr;self->text_=nullptr;self->shown_.clear();if(self->font_){DeleteObject(self->font_);self->font_=nullptr;}return 0;
  }
  return DefWindowProcW(h,message,w,l);
 }
};
}
