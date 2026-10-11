#pragma once
#include "TranscriptView.hpp"
#include "SessionAi.hpp"
#include "AiProviderSettings.hpp"
#include "AiServiceHealth.hpp"
#include "SessionArchive.hpp"
#include <array>
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
  auto add=[&](const wchar_t* type,const wchar_t* label,DWORD style,int id){if(wcscmp(type,L"BUTTON")==0)style|=BS_OWNERDRAW;if(wcscmp(type,L"COMBOBOX")==0)style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;auto h=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window_,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);if(wcscmp(type,L"BUTTON")==0)UiTheme::InstallHover(h);if(wcscmp(type,L"COMBOBOX")==0)UiTheme::InstallCombo(h);if(wcscmp(type,L"EDIT")==0)UiTheme::InstallEdit(h);return h;};
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
  for(auto [id,label]:{std::pair{11,UiLabel(L"当前会话",L"Session")},std::pair{12,UiLabel(L"历史记录",L"History")},std::pair{13,UiLabel(L"服务设置",L"Service settings")},std::pair{14,UiLabel(L"保存记录",L"Save record")},std::pair{18,UiLabel(L"返回会话",L"Back to session")},std::pair{26,UiLabel(L"保存设置",L"Save settings")}})extra_[id]=add(L"BUTTON",label,WS_TABSTOP,id);
  extra_[28]=add(L"BUTTON",UiLabel(L"\u68c0\u67e5\u672c\u5730\u670d\u52a1",L"Check local service"),WS_TABSTOP,28);
  for(int id:{15,19,27,29})extra_[id]=add(L"STATIC",L"",0,id);
  extra_[16]=add(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,16);
  extra_[17]=add(L"EDIT",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,17);SendMessageW(extra_[17],EM_SETLIMITTEXT,1048576,0);
  SetAccessibleName(extra_[16],UiLabel(L"已保存的记录",L"Saved records"));SetAccessibleName(extra_[17],UiLabel(L"历史记录内容",L"Saved record content"));
  const wchar_t* labels[]={UiLabel(L"服务地址",L"Service URL"),UiLabel(L"访问密钥",L"API key"),UiLabel(L"语音识别模型",L"Transcription model"),UiLabel(L"摘要与翻译模型",L"Summary and translation model"),UiLabel(L"朗读模型（选填）",L"Speech model (optional)"),UiLabel(L"朗读音色（选填）",L"Voice (optional)")};
  for(int i=0;i<6;++i){extra_[30+i]=add(L"STATIC",labels[i],0,30+i);extra_[20+i]=add(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|(i==1?ES_PASSWORD:0),20+i);SendMessageW(extra_[20+i],EM_SETLIMITTEXT,8192,0);SetAccessibleName(extra_[20+i],labels[i]);}
  // Tab order follows the row-major sequence used by Layout.
  for(HWND control:{toggle_,summary_,keywords_,speak_,language_,translate_,autoTranslate_,stopSpeech_,text_})SetWindowPos(control,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  for(int id:{13,12,11})SetWindowPos(extra_[id],HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  SetWindowPos(extra_[14],HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  for(int id:{20,21,22,23,24,25,26,28})SetWindowPos(extra_[id],HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  SendMessageW(text_,EM_SETLIMITTEXT,200000,0);Layout();SetTimer(window_,1,300,nullptr);Refresh();ShowWindow(window_,SW_SHOW);
 }
 void Place(RECT r){if(window_){UpdateDpi();MoveWindow(window_,r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);}}
 void Hide(){if(window_)ShowWindow(window_,SW_HIDE);}
 void Close(){health_.Invalidate();shownHealth_.clear();session.Enable(false);if(!speech_.empty()){PlaySoundW(nullptr,nullptr,0);speech_.clear();}if(window_)DestroyWindow(window_);}
private:
 Context context_;
 bool embedded_{false};
 AiServiceHealth health_;std::wstring shownHealth_;
 void RefreshHealth(){
  if(!extra_[28]||!extra_[29])return;
  const auto state=health_.Status();
  const wchar_t* message=nullptr;
  if(settingsDirty_)message=UiLabel(L"\u8bf7\u5148\u4fdd\u5b58\u4fee\u6539\uff0c\u518d\u68c0\u67e5\u672c\u5730 AI \u670d\u52a1\u3002",L"Save changes before checking the local AI service.");
  else switch(state){
   case AiServiceHealth::State::Idle:message=UiLabel(L"\u4fdd\u5b58\u540e\u53ef\u68c0\u67e5\u672c\u5730 AI \u670d\u52a1\u3002\u6b64\u68c0\u67e5\u4e0d\u9a8c\u8bc1\u771f\u5b9e AI \u670d\u52a1\u5546\u8fde\u63a5\u3002",L"Check the local AI service after saving. This does not verify the AI provider connection.");break;
   case AiServiceHealth::State::Checking:message=UiLabel(L"\u6b63\u5728\u68c0\u67e5\u672c\u5730 AI \u670d\u52a1\uff0c\u4e0d\u4f1a\u53d1\u9001\u8bf7\u6c42\u5230 AI \u670d\u52a1\u5546\u3002",L"Checking the local AI service. No request is sent to the AI provider.");break;
   case AiServiceHealth::State::Unavailable:message=UiLabel(L"\u672c\u5730 AI \u670d\u52a1\u4e0d\u53ef\u7528\u3002\u8bf7\u5148\u542f\u52a8\u670d\u52a1\uff0c\u518d\u6b21\u68c0\u67e5\u3002",L"Local AI service unavailable. Start the service, then check again.");break;
   case AiServiceHealth::State::ConfigurationRequired:message=UiLabel(L"\u672c\u5730\u670d\u52a1\u5df2\u8fd0\u884c\uff0c\u670d\u52a1\u5546\u914d\u7f6e\u4e0d\u5b8c\u6574\u3002\u8bf7\u586b\u5199\u5fc5\u586b\u9879\u5e76\u4fdd\u5b58\uff0c\u518d\u6b21\u68c0\u67e5\u3002",L"Local service is running; provider settings are incomplete. Complete required fields, save and check again.");break;
   case AiServiceHealth::State::Ready:message=UiLabel(L"\u672c\u5730\u670d\u52a1\u5df2\u8fd0\u884c\u4e14\u5df2\u914d\u7f6e\u3002\u5c1a\u672a\u9a8c\u8bc1\u771f\u5b9e AI \u670d\u52a1\u5546\u8fde\u63a5\u3002",L"Local service is running and configured. AI provider connectivity is not verified.");break;
   case AiServiceHealth::State::InvalidResponse:message=UiLabel(L"\u672c\u5730\u670d\u52a1\u8fd4\u56de\u5f02\u5e38\u54cd\u5e94\u3002\u8bf7\u68c0\u67e5\u670d\u52a1\u7248\u672c\u540e\u91cd\u8bd5\u3002",L"Local service returned an unexpected response. Check the service version and try again.");break;
  }
  const bool enabled=!settingsDirty_&&!health_.Busy();
  if(bool(IsWindowEnabled(extra_[28]))!=enabled)EnableWindow(extra_[28],enabled);
  if(message&&shownHealth_!=message){shownHealth_=message;SetWindowTextW(extra_[29],message);if(page_==13)Layout();}
 }
 int page_{11};bool settingsLoaded_{false},settingsDirty_{false},loadingSettings_{false};std::array<HWND,36> extra_{};std::vector<SessionArchive::Entry> archives_;
 std::string ArchiveContext()const{return context_==Context::Meeting?"meeting":"call";}
 std::wstring Field(int id){const int n=GetWindowTextLengthW(extra_[id]);std::wstring s(size_t(n)+1,L'\0');GetWindowTextW(extra_[id],s.data(),n+1);s.resize(n);return s;}
 void LoadSettings(){if(settingsDirty_)return;health_.Invalidate();AiProviderSettings config;std::wstring error;loadingSettings_=true;bool ok=LoadAiProviderSettings(config,error);auto fields=ProviderSettingsDetail::Fields(config);for(int i=0;i<6;++i)SetWindowTextW(extra_[20+i],fields[i]->c_str());loadingSettings_=false;settingsLoaded_=true;SetWindowTextW(extra_[27],ok?UiLabel(L"密钥将加密保存在当前 Windows 用户下。保存后，本地 AI 服务将在下一次请求时读取。",L"The key is encrypted for this Windows user. The local AI service reads saved settings on its next request."):error.c_str());}
 void LoadArchive(){auto index=SendMessageW(extra_[16],CB_GETCURSEL,0,0);std::string text;std::wstring error;if(index>=0&&size_t(index)<archives_.size()&&SessionArchive::Load(ArchiveContext(),archives_[index].id,text,error)){SetWindowTextW(extra_[17],DisplayLines(Wide(text)).c_str());SetWindowTextW(extra_[19],UiLabel(L"已保存到本机，仅当前工作区的记录显示在这里。",L"Saved on this computer. Only records from this workspace are listed."));}else{SetWindowTextW(extra_[17],L"");SetWindowTextW(extra_[19],error.empty()?UiLabel(L"暂无记录。在当前会话中选择“保存记录”后，可在这里重新查看。",L"No saved records. Choose Save record in a session to find it here later."):error.c_str());}UpdateTranscriptScrollbar(extra_[17]);}
 void RefreshArchives(){std::wstring error;archives_=SessionArchive::List(ArchiveContext(),error);SendMessageW(extra_[16],CB_RESETCONTENT,0,0);for(const auto& entry:archives_)SendMessageW(extra_[16],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(entry.title.c_str()));EnableWindow(extra_[16],!archives_.empty());if(!archives_.empty())SendMessageW(extra_[16],CB_SETCURSEL,0,0);LoadArchive();if(!error.empty())SetWindowTextW(extra_[19],error.c_str());}
 void SwitchPage(int page){page_=page;if(page==12)RefreshArchives();if(page==13){LoadSettings();RefreshHealth();}Layout();SetFocus(extra_[page]);}
 bool ExtraCommand(WPARAM w){const int id=LOWORD(w);if(id>=11&&id<=13){SwitchPage(id);return true;}if(id==18){SwitchPage(11);return true;}if(id==16&&HIWORD(w)==CBN_SELCHANGE){LoadArchive();return true;}
  if(id>=20&&id<=25&&HIWORD(w)==EN_CHANGE){if(settingsLoaded_&&!loadingSettings_){settingsDirty_=true;health_.Invalidate();SetWindowTextW(extra_[27],UiLabel(L"有未保存的修改。",L"Unsaved changes."));RefreshHealth();}return true;}
  if(id==14){if(page_!=11)return true;std::wstring error;auto value=session.ArchiveText();if(value.empty())error=UiLabel(L"当前没有可保存的记录。请先启用字幕并产生内容。",L"No content to save. Start captions and collect a transcript first.");else if(SessionArchive::Save(ArchiveContext(),value,error))error=UiLabel(L"记录已保存，可在“历史记录”中查看。",L"Record saved. Open History to view it.");SetWindowTextW(extra_[15],error.c_str());return true;}
  if(id==26){if(page_!=13)return true;AiProviderSettings config;auto fields=ProviderSettingsDetail::Fields(config);for(int i=0;i<6;++i)*fields[i]=Field(20+i);std::wstring error;if(SaveAiProviderSettings(config,error)){settingsDirty_=false;health_.Invalidate();error=UiLabel(L"设置已保存。尚未验证真实服务连接；请确认本地 AI 服务已运行。",L"Settings saved. Connection is not yet verified; ensure the local AI service is running.");}SetWindowTextW(extra_[27],error.c_str());RefreshHealth();Layout();return true;}if(id==28){if(page_!=13)return true;if(!settingsDirty_)health_.Check();RefreshHealth();return true;}return false;
 }

 UINT dpi_{96};HFONT title_{};HBRUSH background_=CreateSolidBrush(UiTheme::Background),editBackground_=CreateSolidBrush(UiTheme::Surface);int transcriptY_{300};
 int S(int value)const{return MulDiv(value,int(dpi_),96);}
 void UpdateDpi(){const auto next=GetDpiForWindow(window_);if(next==dpi_)return;dpi_=next;auto old=font_;font_=UiTheme::Font(S(14));if(title_)DeleteObject(title_);title_=UiTheme::Font(S(24),600);for(HWND child=GetWindow(window_,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);if(old)DeleteObject(old);Layout();}
 void Paint(HDC dc){RECT r;GetClientRect(window_,&r);UiTheme::Fill(dc,r,UiTheme::Background);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,UiTheme::Text);auto old=SelectObject(dc,title_);RECT label{S(28),S(24),r.right-S(28),S(62)};DrawTextW(dc,UiLabel(L"\u667a\u80fd\u52a9\u624b",L"AI assistant"),-1,&label,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,font_);SetTextColor(dc,UiTheme::Muted);label={S(28),S(65),r.right-S(28),S(90)};DrawTextW(dc,(context_==Context::Meeting?UiLabel(L"当前工作区：视频会议 · 记录与通话工作区独立",L"Meeting workspace · Records are separate from calls"):UiLabel(L"当前工作区：视频通话 · 记录与会议工作区独立",L"Call workspace · Records are separate from meetings")),-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS);if(page_==11){label={S(28),S(transcriptY_-30),r.right-S(28),S(transcriptY_-6)};DrawTextW(dc,UiLabel(L"\u4f1a\u8bdd\u8bb0\u5f55\u4e0e\u751f\u6210\u7ed3\u679c",L"Transcript and generated results"),-1,&label,DT_SINGLELINE);}SelectObject(dc,old);}

 std::uint64_t speechRevision_{0};
 HWND window_{},note_{},toggle_{},summary_{},text_{},language_{},translate_{},speak_{},stopSpeech_{},keywords_{},autoTranslate_{};HFONT font_{};std::string shown_,speech_;
 static std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
 // Native multiline EDIT controls require CRLF; archive bytes remain unchanged.
 static std::wstring DisplayLines(const std::wstring& text){std::wstring out;out.reserve(text.size());for(size_t i=0;i<text.size();++i){if(text[i]==L'\r'){out+=L"\r\n";if(i+1<text.size()&&text[i+1]==L'\n')++i;}else if(text[i]==L'\n')out+=L"\r\n";else out+=text[i];}return out;}
 void UpdateTranscriptScroll(){if(text_)UpdateTranscriptScrollbar(text_);}

 void Layout(){
  if(!text_)return;RECT r{};GetClientRect(window_,&r);
  const int width=MulDiv(r.right,96,int(dpi_)),height=MulDiv(r.bottom,96,int(dpi_));
  const int margin=28,gap=8,rowPitch=48,contentWidth=std::max(1,width-2*margin);
  auto position=[&](HWND h,int x,int y,int w,int ht){MoveWindow(h,S(x),S(y),S(std::max(1,w)),S(std::max(1,ht)),TRUE);};
  for(int id=1;id<=10;++id)ShowWindow(GetDlgItem(window_,id),page_==11?SW_SHOW:SW_HIDE);
  for(int id=11;id<36;++id){if(!extra_[id])continue;const bool show=id<=13||(page_==11&&(id==14||id==15))||(page_==12&&id>=16&&id<=19)||(page_==13&&id>=20);ShowWindow(extra_[id],show?SW_SHOW:SW_HIDE);}
  for(int i=0;i<3;++i)position(extra_[11+i],margin+i*156,102,148,36);
  if(page_==12){position(extra_[16],margin,166,contentWidth-164,220);UiTheme::SetComboDpi(extra_[16],dpi_);position(extra_[18],width-margin-148,166,148,40);position(extra_[19],margin,218,contentWidth,48);position(extra_[17],margin,276,contentWidth,height-296);UpdateTranscriptScrollbar(extra_[17]);InvalidateRect(window_,nullptr,FALSE);return;}
  if(page_==13){
   const int cellWidth=(contentWidth-24)/2,fieldPitch=height<580?72:80;
   for(int i=0;i<6;++i){int x=margin+(i%2)*(cellWidth+24),y=160+(i/2)*fieldPitch;position(extra_[30+i],x,y,cellWidth,24);position(extra_[20+i],x,y+28,cellWidth,38);}
   const int actionsY=160+2*fieldPitch+80,saveY=actionsY+50;
   position(extra_[26],margin,actionsY,160,38);position(extra_[28],margin+172,actionsY,184,38);
   // Measure both messages independently so save errors and health guidance stay distinct.
   int saveHeight=36,healthHeight=36;
   if(auto dc=GetDC(window_)){
    const auto old=SelectObject(dc,font_);
    auto measure=[&](int id){auto value=Field(id);RECT measured{0,0,S(contentWidth),0};DrawTextW(dc,value.c_str(),-1,&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);return std::max(20,int((measured.bottom*96+int(dpi_)-1)/int(dpi_))+2);};
    saveHeight=measure(27);healthHeight=measure(29);SelectObject(dc,old);ReleaseDC(window_,dc);
   }
   position(extra_[27],margin,saveY,contentWidth,saveHeight);position(extra_[29],margin,saveY+saveHeight+10,contentWidth,healthHeight);
   InvalidateRect(window_,nullptr,FALSE);return;
  }
  position(extra_[14],margin,height-58,140,36);position(extra_[15],margin+156,height-62,contentWidth-156,48);
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
  const int actionsY=160+noteHeight+16,rows=(8+cols-1)/cols;
  auto move=[&](HWND h,int x,int y,int w,int ht){MoveWindow(h,S(x),S(y),S(std::max(1,w)),S(std::max(1,ht)),TRUE);};
  move(note_,margin,160,contentWidth,noteHeight);UiTheme::SetComboDpi(language_,dpi_);
  for(int i=0;i<8;++i)move(actions[i],margin+(i%cols)*(cell+gap),actionsY+(i/cols)*rowPitch,cell,i==4?180:36);
  transcriptY_=actionsY+rows*rowPitch+30;move(text_,margin,transcriptY_,contentWidth,height-transcriptY_-78);
  UpdateTranscriptScroll();InvalidateRect(window_,nullptr,FALSE);
 }

 void Refresh(){RefreshHealth();EnableWindow(language_,!session.AutoTranslating());SetWindowTextW(autoTranslate_,session.AutoTranslating()?UiLabel(L"\u505c\u6b62\u81ea\u52a8\u7ffb\u8bd1",L"Stop auto translation"):UiLabel(L"\u5f00\u542f\u81ea\u52a8\u7ffb\u8bd1",L"Start auto translation"));auto voice=session.TakeSpeechPacket();if(!speech_.empty()&&speechRevision_!=voice.revision){PlaySoundW(nullptr,nullptr,0);speech_.clear();}speechRevision_=voice.revision;if(!voice.audio.empty()){PlaySoundW(nullptr,nullptr,0);speech_=std::move(voice.audio);PlaySoundW(reinterpret_cast<LPCWSTR>(speech_.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);}SetWindowTextW(toggle_,session.Enabled()?UiLabel(L"\u505c\u6b62\u5b57\u5e55",L"Stop captions"):UiLabel(L"\u542f\u7528\u5b57\u5e55",L"Start captions"));auto value=session.Text();if(value!=shown_){shown_=value;ReplaceTranscriptText(text_,DisplayLines(Wide(value)).c_str());}}
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
   case WM_DRAWITEM:{auto d=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(d->CtlType==ODT_COMBOBOX){UiTheme::DrawComboItem(*d,self->font_);return TRUE;}UiTheme::DrawButton(*d,self->font_,d->CtlID==2||d->CtlID==26,d->CtlID==self->page_,d->CtlID==8);return TRUE;}
   case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,reinterpret_cast<HWND>(l)==self->note_?UiTheme::Muted:UiTheme::Text);const int id=GetDlgCtrlID(reinterpret_cast<HWND>(l));const bool edit=!(id==1||id==15||id==19||id==27||id==29||id>=30);SetBkColor(dc,edit?UiTheme::Surface:UiTheme::Background);return reinterpret_cast<LRESULT>(edit?self->editBackground_:self->background_);}

   case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={self->S(640),self->S(600)};return 0;
   case WM_TIMER:self->Refresh();return 0;
   case WM_COMMAND:if(self->ExtraCommand(w))return 0;if(self->page_!=11)return 0;if(LOWORD(w)==10){const char* langs[]={"en","zh","ja","ko","es"};auto index=SendMessageW(self->language_,CB_GETCURSEL,0,0);if(self->session.AutoTranslating())self->session.AutoTranslate("");else if(index>=0&&index<5)self->session.AutoTranslate(langs[index]);self->Refresh();}else if(LOWORD(w)==9){self->session.Keywords();self->Refresh();}else if(LOWORD(w)==7){self->session.Speak();}else if(LOWORD(w)==8){self->session.StopSpeech();PlaySoundW(nullptr,nullptr,0);self->speech_.clear();self->Refresh();}else if(LOWORD(w)==6){const char* langs[]={"en","zh","ja","ko","es"};auto index=SendMessageW(self->language_,CB_GETCURSEL,0,0);if(index>=0&&index<5)self->session.Translate(langs[index]);self->Refresh();}else if(LOWORD(w)==2){self->session.Enable(!self->session.Enabled());self->Refresh();}else if(LOWORD(w)==3){self->session.Summarize();self->Refresh();}return 0;
   case WM_CLOSE:self->Close();return 0;
   case WM_DESTROY:self->health_.Invalidate();self->shownHealth_.clear();if(!self->speech_.empty()){PlaySoundW(nullptr,nullptr,0);self->speech_.clear();}KillTimer(h,1);self->window_=nullptr;self->text_=nullptr;self->shown_.clear();self->page_=11;self->settingsLoaded_=false;self->settingsDirty_=false;self->loadingSettings_=false;self->archives_.clear();self->extra_={};if(self->font_){DeleteObject(self->font_);self->font_=nullptr;}if(self->title_){DeleteObject(self->title_);self->title_=nullptr;}return 0;
  }
  return DefWindowProcW(h,message,w,l);
 }
};
}
