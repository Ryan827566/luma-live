#pragma once
#include "SessionAi.hpp"
#include "UiTheme.hpp"
#include "UiAccessibility.hpp"
#include <mmsystem.h>
namespace luma::client::ui::preview {
class AiPanel {
public:
 enum class Context {Call,Meeting};
 explicit AiPanel(Context context=Context::Call):context_(context){}
 SessionAi session;
 ~AiPanel(){Close();DeleteObject(background_);DeleteObject(editBackground_);}
 void Open(HWND owner,bool embedded=false){
  if(window_){ShowWindow(window_,SW_SHOW);if(!embedded_)SetForegroundWindow(window_);return;}
  embedded_=embedded;
  WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Proc;cls.lpszClassName=L"LumaSessionAi";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=nullptr;RegisterClassW(&cls);
  window_=CreateWindowExW(embedded_?WS_EX_CONTROLPARENT:0,cls.lpszClassName,UiLabel(L"AI \u5b57\u5e55\u4e0e\u4f1a\u8bae\u6458\u8981",L"AI captions and summaries"),embedded_?(WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS):WS_OVERLAPPEDWINDOW,embedded_?0:CW_USEDEFAULT,embedded_?0:CW_USEDEFAULT,720,560,owner,nullptr,cls.hInstance,this);
  if(!window_)return;
  dpi_=GetDpiForWindow(window_);font_=UiTheme::Font(S(14));title_=UiTheme::Font(S(24),600);
  auto add=[&](const wchar_t* type,const wchar_t* label,DWORD style,int id){if(wcscmp(type,L"BUTTON")==0)style|=BS_OWNERDRAW;if(wcscmp(type,L"COMBOBOX")==0)style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;auto h=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window_,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);if(wcscmp(type,L"BUTTON")==0)UiTheme::InstallHover(h);if(wcscmp(type,L"COMBOBOX")==0)UiTheme::InstallCombo(h);return h;};
  note_=add(L"STATIC",UiLabel(L"\u9ed8\u8ba4\u5173\u95ed\u3002\u542f\u7528\u540e\uff0c\u672c\u6b21\u901a\u8bdd\u5404\u65b9\u8bed\u97f3\u5c06\u53d1\u9001\u81f3\u5df2\u914d\u7f6e\u7684 AI \u670d\u52a1\u3002\u8bf7\u544a\u77e5\u53c2\u4f1a\u8005\u3002\u5b57\u5e55\u7ea6\u6bcf 5 \u79d2\u66f4\u65b0\uff0c\u505c\u6b62\u540e\u4e22\u5f03\u672a\u5904\u7406\u7247\u6bb5\u3002",L"Off by default. Enabling sends participant audio to your configured AI service. Inform participants first. Captions update about every five seconds. Stopping discards pending segments."),0,1);
  toggle_=add(L"BUTTON",UiLabel(L"\u542f\u7528\u5b57\u5e55",L"Start captions"),WS_TABSTOP,2);summary_=add(L"BUTTON",UiLabel(L"\u751f\u6210\u6458\u8981\u4e0e\u5f85\u529e",L"Summary and actions"),WS_TABSTOP,3);
  language_=add(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,5);for(auto label:{UiLabel(L"\u82f1\u8bed",L"English"),UiLabel(L"\u4e2d\u6587",L"Chinese"),UiLabel(L"\u65e5\u8bed",L"Japanese"),UiLabel(L"\u97e9\u8bed",L"Korean"),UiLabel(L"\u897f\u73ed\u7259\u8bed",L"Spanish")})SendMessageW(language_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(language_,CB_SETCURSEL,0,0);
  translate_=add(L"BUTTON",UiLabel(L"\u7ffb\u8bd1\u8bb0\u5f55",L"Translate transcript"),WS_TABSTOP,6);
  speak_=add(L"BUTTON",UiLabel(L"AI \u6717\u8bfb\u6458\u8981",L"Read AI summary"),WS_TABSTOP,7);
  stopSpeech_=add(L"BUTTON",UiLabel(L"\u505c\u6b62\u6717\u8bfb",L"Stop reading"),WS_TABSTOP,8);
  keywords_=add(L"BUTTON",UiLabel(L"\u63d0\u53d6\u5173\u952e\u8bcd",L"Extract keywords"),WS_TABSTOP,9);
  autoTranslate_=add(L"BUTTON",UiLabel(L"\u5f00\u542f\u81ea\u52a8\u7ffb\u8bd1",L"Start auto translation"),WS_TABSTOP,10);
  text_=add(L"EDIT",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,4);
  SetAccessibleName(language_,UiLabel(L"翻译目标语言",L"Translation language"));
  SetAccessibleName(text_,UiLabel(L"会话记录与生成结果",L"Transcript and generated results"));
  // Tab order follows the row-major sequence used by Layout.
  for(HWND control:{toggle_,summary_,keywords_,speak_,language_,translate_,autoTranslate_,stopSpeech_,text_})SetWindowPos(control,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  SendMessageW(text_,EM_SETLIMITTEXT,200000,0);Layout();SetTimer(window_,1,300,nullptr);Refresh();ShowWindow(window_,SW_SHOW);
 }
 void Place(RECT r){if(window_){UpdateDpi();MoveWindow(window_,r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);}}
 void Hide(){if(window_)ShowWindow(window_,SW_HIDE);}
 void Close(){session.Enable(false);if(!speech_.empty()){PlaySoundW(nullptr,nullptr,0);speech_.clear();}if(window_)DestroyWindow(window_);}
private:
 Context context_;
 bool embedded_{false};
 UINT dpi_{96};HFONT title_{};HBRUSH background_=CreateSolidBrush(UiTheme::Background),editBackground_=CreateSolidBrush(UiTheme::Surface);int transcriptY_{300};
 int S(int value)const{return MulDiv(value,int(dpi_),96);}
 void UpdateDpi(){const auto next=GetDpiForWindow(window_);if(next==dpi_)return;dpi_=next;auto old=font_;font_=UiTheme::Font(S(14));if(title_)DeleteObject(title_);title_=UiTheme::Font(S(24),600);for(HWND child=GetWindow(window_,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);if(old)DeleteObject(old);Layout();}
 void Paint(HDC dc){RECT r;GetClientRect(window_,&r);UiTheme::Fill(dc,r,UiTheme::Background);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,UiTheme::Text);auto old=SelectObject(dc,title_);RECT label{S(28),S(24),r.right-S(28),S(62)};DrawTextW(dc,UiLabel(L"\u667a\u80fd\u52a9\u624b",L"AI assistant"),-1,&label,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,font_);SetTextColor(dc,UiTheme::Muted);label={S(28),S(65),r.right-S(28),S(90)};DrawTextW(dc,(context_==Context::Meeting?UiLabel(L"当前工作区：视频会议 · 记录与通话工作区独立",L"Meeting workspace · Records are separate from calls"):UiLabel(L"当前工作区：视频通话 · 记录与会议工作区独立",L"Call workspace · Records are separate from meetings")),-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS);label={S(28),S(transcriptY_-30),r.right-S(28),S(transcriptY_-6)};DrawTextW(dc,UiLabel(L"\u4f1a\u8bdd\u8bb0\u5f55\u4e0e\u751f\u6210\u7ed3\u679c",L"Transcript and generated results"),-1,&label,DT_SINGLELINE);SelectObject(dc,old);}

 std::uint64_t speechRevision_{0};
 HWND window_{},note_{},toggle_{},summary_{},text_{},language_{},translate_{},speak_{},stopSpeech_{},keywords_{},autoTranslate_{};HFONT font_{};std::string shown_,speech_;
 static std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
 void UpdateTranscriptScroll(){if(!text_)return;auto dc=GetDC(text_);if(!dc)return;auto old=SelectObject(dc,font_);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(text_,dc);RECT r{};GetClientRect(text_,&r);const int lines=int(SendMessageW(text_,EM_GETLINECOUNT,0,0));const int visible=std::max(1,int(r.bottom)/std::max(1,int(metrics.tmHeight)));ShowScrollBar(text_,SB_VERT,lines>visible);}
 void Layout(){
  if(!text_)return;RECT r{};GetClientRect(window_,&r);
  const int width=MulDiv(r.right,96,int(dpi_)),height=MulDiv(r.bottom,96,int(dpi_));
  const int margin=28,gap=8,rowPitch=48,contentWidth=std::max(1,width-2*margin);
  HWND actions[]={toggle_,summary_,keywords_,speak_,language_,translate_,autoTranslate_,stopSpeech_};
  int noteHeight=64,minCell=180;
  if(auto dc=GetDC(window_)){
   const auto old=SelectObject(dc,font_);auto logicalCeil=[&](int pixels){return (pixels*96+int(dpi_)-1)/int(dpi_);};
   const int length=GetWindowTextLengthW(note_);std::wstring note(size_t(length)+1,L'\0');GetWindowTextW(note_,note.data(),length+1);
   RECT measured{0,0,S(contentWidth),0};DrawTextW(dc,note.c_str(),-1,&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);noteHeight=logicalCeil(measured.bottom)+2;
   int widest=0;auto measure=[&](const wchar_t* label){SIZE extent{};GetTextExtentPoint32W(dc,label,int(wcslen(label)),&extent);widest=std::max(widest,int(extent.cx));};
   for(auto action:actions){wchar_t label[256]{};GetWindowTextW(action,label,256);measure(label);}
   measure(UiLabel(L"停止自动翻译",L"Stop auto translation"));measure(UiLabel(L"开启自动翻译",L"Start auto translation"));measure(UiLabel(L"停止字幕",L"Stop captions"));
   minCell=logicalCeil(widest)+32;SelectObject(dc,old);ReleaseDC(window_,dc);
  }
  const int cols=std::clamp((contentWidth+gap)/(minCell+gap),2,4),cell=(contentWidth-gap*(cols-1))/cols;
  const int actionsY=102+noteHeight+16,rows=(8+cols-1)/cols;
  auto move=[&](HWND h,int x,int y,int w,int ht){MoveWindow(h,S(x),S(y),S(std::max(1,w)),S(std::max(1,ht)),TRUE);};
  move(note_,margin,102,contentWidth,noteHeight);UiTheme::SetComboDpi(language_,dpi_);
  for(int i=0;i<8;++i)move(actions[i],margin+(i%cols)*(cell+gap),actionsY+(i/cols)*rowPitch,cell,i==4?180:36);
  transcriptY_=actionsY+rows*rowPitch+30;move(text_,margin,transcriptY_,contentWidth,height-transcriptY_-20);
  UpdateTranscriptScroll();InvalidateRect(window_,nullptr,FALSE);
 }

 void Refresh(){EnableWindow(language_,!session.AutoTranslating());SetWindowTextW(autoTranslate_,session.AutoTranslating()?UiLabel(L"\u505c\u6b62\u81ea\u52a8\u7ffb\u8bd1",L"Stop auto translation"):UiLabel(L"\u5f00\u542f\u81ea\u52a8\u7ffb\u8bd1",L"Start auto translation"));auto voice=session.TakeSpeechPacket();if(!speech_.empty()&&speechRevision_!=voice.revision){PlaySoundW(nullptr,nullptr,0);speech_.clear();}speechRevision_=voice.revision;if(!voice.audio.empty()){PlaySoundW(nullptr,nullptr,0);speech_=std::move(voice.audio);PlaySoundW(reinterpret_cast<LPCWSTR>(speech_.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);}SetWindowTextW(toggle_,session.Enabled()?UiLabel(L"\u505c\u6b62\u5b57\u5e55",L"Stop captions"):UiLabel(L"\u542f\u7528\u5b57\u5e55",L"Start captions"));auto value=session.Text();if(value!=shown_){DWORD start=0,end=0;SendMessageW(text_,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));const int oldLength=GetWindowTextLengthW(text_);const int firstLine=int(SendMessageW(text_,EM_GETFIRSTVISIBLELINE,0,0));const bool follow=start==end&&end==DWORD(oldLength);shown_=value;SetWindowTextW(text_,Wide(value).c_str());UpdateTranscriptScroll();if(follow){SendMessageW(text_,EM_SETSEL,WPARAM(-1),LPARAM(-1));SendMessageW(text_,EM_SCROLLCARET,0,0);}else{SendMessageW(text_,EM_SETSEL,start,end);SendMessageW(text_,EM_LINESCROLL,0,firstLine);}}}
 static LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM w,LPARAM l){
  auto self=reinterpret_cast<AiPanel*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<AiPanel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(h,message,w,l);
  switch(message){
   case WM_SIZE:self->Layout();return 0;
   case WM_DPICHANGED_AFTERPARENT:self->UpdateDpi();return 0;
   case WM_DPICHANGED:self->UpdateDpi();if(!self->embedded_){auto r=reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}return 0;
   case WM_ERASEBKGND:return 1;
   case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);self->Paint(dc);EndPaint(h,&ps);return 0;}
   case WM_PRINTCLIENT:self->Paint(reinterpret_cast<HDC>(w));return 0;
   case WM_MEASUREITEM:{auto d=reinterpret_cast<MEASUREITEMSTRUCT*>(l);if(d->CtlType==ODT_COMBOBOX){d->itemHeight=self->S(30);return TRUE;}break;}
   case WM_DRAWITEM:{auto d=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(d->CtlType==ODT_COMBOBOX){UiTheme::DrawComboItem(*d,self->font_);return TRUE;}UiTheme::DrawButton(*d,self->font_,d->CtlID==2,false,d->CtlID==8);return TRUE;}
   case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,reinterpret_cast<HWND>(l)==self->note_?UiTheme::Muted:UiTheme::Text);const bool edit=reinterpret_cast<HWND>(l)!=self->note_;SetBkColor(dc,edit?UiTheme::Surface:UiTheme::Background);return reinterpret_cast<LRESULT>(edit?self->editBackground_:self->background_);}

   case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={self->S(640),self->S(600)};return 0;
   case WM_TIMER:self->Refresh();return 0;
   case WM_COMMAND:if(LOWORD(w)==10){const char* langs[]={"en","zh","ja","ko","es"};auto index=SendMessageW(self->language_,CB_GETCURSEL,0,0);if(self->session.AutoTranslating())self->session.AutoTranslate("");else if(index>=0&&index<5)self->session.AutoTranslate(langs[index]);self->Refresh();}else if(LOWORD(w)==9){self->session.Keywords();self->Refresh();}else if(LOWORD(w)==7){self->session.Speak();}else if(LOWORD(w)==8){self->session.StopSpeech();PlaySoundW(nullptr,nullptr,0);self->speech_.clear();self->Refresh();}else if(LOWORD(w)==6){const char* langs[]={"en","zh","ja","ko","es"};auto index=SendMessageW(self->language_,CB_GETCURSEL,0,0);if(index>=0&&index<5)self->session.Translate(langs[index]);self->Refresh();}else if(LOWORD(w)==2){self->session.Enable(!self->session.Enabled());self->Refresh();}else if(LOWORD(w)==3){self->session.Summarize();self->Refresh();}return 0;
   case WM_CLOSE:self->Close();return 0;
   case WM_DESTROY:if(!self->speech_.empty()){PlaySoundW(nullptr,nullptr,0);self->speech_.clear();}KillTimer(h,1);self->window_=nullptr;self->text_=nullptr;self->shown_.clear();if(self->font_){DeleteObject(self->font_);self->font_=nullptr;}if(self->title_){DeleteObject(self->title_);self->title_=nullptr;}return 0;
  }
  return DefWindowProcW(h,message,w,l);
 }
};
}
