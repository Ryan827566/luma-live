#pragma once
#include "SessionAi.hpp"
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
  window_=CreateWindowExW(embedded_?WS_EX_CONTROLPARENT:0,cls.lpszClassName,L"AI 字幕与会议摘要",embedded_?(WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS):WS_OVERLAPPEDWINDOW,embedded_?0:CW_USEDEFAULT,embedded_?0:CW_USEDEFAULT,720,560,owner,nullptr,cls.hInstance,this);
  if(!window_)return;
  font_=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Microsoft YaHei UI");
  auto add=[&](const wchar_t* type,const wchar_t* label,DWORD style,int id){auto h=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window_,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);return h;};
  note_=add(L"STATIC",L"默认关闭。启用后，本次通话各方语音将发送至已配置的 AI 服务。请告知参会者。字幕约每 5 秒更新，停止后丢弃未处理片段。",0,1);
  toggle_=add(L"BUTTON",L"启用字幕",WS_TABSTOP,2);summary_=add(L"BUTTON",L"生成摘要与待办",WS_TABSTOP,3);
  language_=add(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,5);for(auto label:{L"English",L"中文",L"日本語",L"한국어",L"Español"})SendMessageW(language_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(language_,CB_SETCURSEL,0,0);
  translate_=add(L"BUTTON",L"翻译记录",WS_TABSTOP,6);
  speak_=add(L"BUTTON",L"AI 朗读摘要",WS_TABSTOP,7);
  stopSpeech_=add(L"BUTTON",L"停止朗读",WS_TABSTOP,8);
  text_=add(L"EDIT",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,4);
  SendMessageW(text_,EM_SETLIMITTEXT,200000,0);Layout();SetTimer(window_,1,300,nullptr);Refresh();ShowWindow(window_,SW_SHOW);
 }
 void Place(RECT r){if(window_)MoveWindow(window_,r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);}
 void Hide(){if(window_)ShowWindow(window_,SW_HIDE);}
 void Close(){session.Enable(false);if(!speech_.empty()){PlaySoundW(nullptr,nullptr,0);speech_.clear();}if(window_)DestroyWindow(window_);}
private:
 bool embedded_{false};
 HWND window_{},note_{},toggle_{},summary_{},text_{},language_{},translate_{},speak_{},stopSpeech_{};HFONT font_{};std::string shown_,speech_;
 static std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
 void Layout(){if(!text_)return;RECT r;GetClientRect(window_,&r);MoveWindow(note_,20,16,r.right-40,68,TRUE);MoveWindow(toggle_,20,90,140,34,TRUE);MoveWindow(summary_,172,90,180,34,TRUE);MoveWindow(language_,20,134,140,180,TRUE);MoveWindow(translate_,172,134,180,34,TRUE);MoveWindow(speak_,366,90,150,34,TRUE);MoveWindow(stopSpeech_,366,134,150,34,TRUE);MoveWindow(text_,20,182,r.right-40,r.bottom-202,TRUE);}
 void Refresh(){auto voice=session.TakeSpeech();if(!voice.empty()){PlaySoundW(nullptr,nullptr,0);speech_=std::move(voice);PlaySoundW(reinterpret_cast<LPCWSTR>(speech_.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);}SetWindowTextW(toggle_,session.Enabled()?L"停止字幕":L"启用字幕");auto value=session.Text();if(value!=shown_){shown_=value;SetWindowTextW(text_,Wide(value).c_str());SendMessageW(text_,EM_SETSEL,WPARAM(-1),LPARAM(-1));SendMessageW(text_,EM_SCROLLCARET,0,0);}}
 static LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM w,LPARAM l){
  auto self=reinterpret_cast<AiPanel*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<AiPanel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(h,message,w,l);
  switch(message){
   case WM_SIZE:self->Layout();return 0;
   case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={560,420};return 0;
   case WM_TIMER:self->Refresh();return 0;
   case WM_COMMAND:if(LOWORD(w)==7){self->session.Speak();}else if(LOWORD(w)==8){PlaySoundW(nullptr,nullptr,0);self->speech_.clear();}else if(LOWORD(w)==6){const char* langs[]={"en","zh","ja","ko","es"};auto index=SendMessageW(self->language_,CB_GETCURSEL,0,0);if(index>=0&&index<5)self->session.Translate(langs[index]);self->Refresh();}else if(LOWORD(w)==2){self->session.Enable(!self->session.Enabled());self->Refresh();}else if(LOWORD(w)==3){self->session.Summarize();self->Refresh();}return 0;
   case WM_CLOSE:self->Close();return 0;
   case WM_DESTROY:if(!self->speech_.empty()){PlaySoundW(nullptr,nullptr,0);self->speech_.clear();}KillTimer(h,1);self->window_=nullptr;self->text_=nullptr;self->shown_.clear();if(self->font_){DeleteObject(self->font_);self->font_=nullptr;}return 0;
  }
  return DefWindowProcW(h,message,w,l);
 }
};
}
