#include "SessionAi.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace luma::client::ui::preview;
using namespace std::chrono_literals;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool Wait(F test){auto until=std::chrono::steady_clock::now()+2s;while(!test()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(5ms);return test();}
int main(){try{
 std::atomic<int> calls{0};
 SessionAi ai([&](const std::wstring& path,const std::string& body,const std::wstring& type){
  ++calls;
  if(path==L"/transcribe"){Check(type==L"audio/wav"&&body.size()==480044&&body.substr(0,4)=="RIFF"&&body.substr(8,4)=="WAVE","invalid WAV request");return SessionAi::Reply{true,"Project update"};}
  if(path==L"/keywords"){Check(body.find("[alex] Project update")!=std::string::npos,"keywords missing transcript");return SessionAi::Reply{true,"Project, release"};}
  Check(path==L"/summary"&&body.find("[alex] Project update")!=std::string::npos,"summary missing attributed transcript");return SessionAi::Reply{true,"Action: review the release"};
 });
 luma::client::media::pipeline::AudioFrame audio;audio.sample_rate=48000;audio.channels=1;audio.format=luma::client::media::pipeline::AudioSampleFormat::S16;audio.data.resize(480000,0);
 ai.Submit("alex",audio);std::this_thread::sleep_for(20ms);Check(calls==0,"default-off uploaded audio");
 Check(!ai.Keywords()&&ai.Status()==AiMessage::NoTranscript,"empty keywords lacks guidance");
 Check(!ai.Speak()&&ai.Status()==AiMessage::NoSummary,"missing summary lacks guidance");
 Check(!ai.Summarize(),"empty summary accepted");ai.Enable(true);ai.Submit("alex",audio);
 Check(Wait([&]{return ai.Text().find("[alex] Project update")!=std::string::npos;}),"transcript missing");
 Check(ai.Summarize(),"summary rejected");Check(Wait([&]{return ai.Text().find("Action: review")!=std::string::npos;}),"summary missing");
 Check(ai.Keywords(),"keywords rejected");Check(Wait([&]{return ai.Text().find("Project, release")!=std::string::npos;}),"keywords missing");
 ai.Enable(false);int before=calls;ai.Submit("alex",audio);std::this_thread::sleep_for(20ms);Check(calls==before,"stop uploaded audio");
 ai.Reset();Check(ai.Text().find("Project update")==std::string::npos&&ai.Text().find("Project, release")==std::string::npos,"new call retained transcript");
 std::atomic<bool> entered{false},release{false};
 SessionAi stale([&](const std::wstring&,const std::string&,const std::wstring&){entered=true;while(!release)std::this_thread::sleep_for(1ms);return SessionAi::Reply{true,"OLD SESSION"};});
 stale.Enable(true);stale.Submit("old",audio);bool started=Wait([&]{return entered.load();});stale.Reset();release=true;Check(started,"worker did not start");
 std::this_thread::sleep_for(30ms);Check(stale.Text().find("OLD SESSION")==std::string::npos,"late result contaminated new session");
 SessionAi failing([](const std::wstring&,const std::string&,const std::wstring&){return SessionAi::Reply{false,"provider not configured"};});
 failing.Enable(true);failing.Submit("alex",audio);Check(Wait([&]{return failing.LastError().find("provider not configured")!=std::string::npos;}),"provider error hidden");Check(failing.Status()==AiMessage::RequestFailed,"missing error state");Check(failing.Text().find("provider not configured")==std::string::npos,"provider diagnostics leaked into localized UI");

 SessionAi longSummary([](const std::wstring& route,const std::string&,const std::wstring&){return SessionAi::Reply{true,route==L"/summary"?std::string(4001,'s'):"speech"};});
 longSummary.Enable(true);longSummary.Submit("alex",audio);Check(Wait([&]{return longSummary.Status()==AiMessage::Transcribing;}),"long summary input missing");
 Check(longSummary.Summarize()&&Wait([&]{return longSummary.Status()==AiMessage::SummaryReady;}),"long summary missing");
 Check(!longSummary.Speak()&&longSummary.Status()==AiMessage::SpeechLimit&&longSummary.Enabled(),"speech limit confused with transcript limit");
 std::atomic<int> chunks{0};std::atomic<bool> cappedEntered{false},cappedRelease{false};
 SessionAi capped([&](const std::wstring& route,const std::string&,const std::wstring&){
  if(route==L"/keywords")return SessionAi::Reply{true,"remaining keywords"};
  if(++chunks==1)return SessionAi::Reply{true,std::string(63000,'x')};
  cappedEntered=true;while(!cappedRelease)std::this_thread::sleep_for(1ms);return SessionAi::Reply{true,std::string(2000,'y')};
 });
 capped.Enable(true);capped.Submit("alex",audio);Check(Wait([&]{return capped.Text().size()>63000;}),"cap test initial transcript missing");
 capped.Submit("alex",audio);bool blocked=Wait([&]{return cappedEntered.load();});bool queued=capped.Keywords();cappedRelease=true;
 Check(blocked&&queued,"keyword not queued behind transcription");Check(Wait([&]{return !capped.Enabled();}),"transcript cap not reached");
 Check(capped.Keywords()&&Wait([&]{return capped.Text().find("remaining keywords")!=std::string::npos;}),"dropped keyword job left actions permanently busy");

 std::atomic<bool> autoEntered{false},autoRelease{false};
 SessionAi automatic([&](const std::wstring& route,const std::string& body,const std::wstring&){
  if(route==L"/transcribe")return SessionAi::Reply{true,"source words"};
  Check(body=="[alex] source words","automatic translation lost speaker");
  if(route==L"/translate?ja"){autoEntered=true;while(!autoRelease)std::this_thread::sleep_for(1ms);return SessionAi::Reply{true,"STALE AUTO"};}
  Check(route==L"/translate?es","automatic target incorrect");return SessionAi::Reply{true,"CURRENT AUTO"};
 });
 Check(!automatic.AutoTranslate("ja"),"automatic translation enabled without captions");
 automatic.Enable(true);Check(automatic.AutoTranslate("ja"),"automatic translation rejected");automatic.Submit("alex",audio);
 bool autoStarted=Wait([&]{return autoEntered.load();});
 Check(automatic.CaptionFor("alex").original=="source words"&&automatic.CaptionFor("other").original.empty(),"caption speaker attribution failed");
 Check(automatic.CaptionFor("alex",std::chrono::steady_clock::now()+11s).original.empty(),"caption failed to expire");automatic.AutoTranslate("");autoRelease=true;Check(autoStarted,"automatic translation not scheduled");
 std::this_thread::sleep_for(30ms);Check(automatic.Text().find("STALE AUTO")==std::string::npos,"disabled translation leaked late result");
 automatic.AutoTranslate("es");automatic.Submit("alex",audio);Check(Wait([&]{return automatic.Text().find("CURRENT AUTO")!=std::string::npos;}),"automatic translation missing");
 Check(automatic.CaptionFor("alex").translated=="CURRENT AUTO","translated caption missing");
 automatic.Enable(false);Check(automatic.CaptionFor("alex").original.empty(),"stop retained caption");Check(!automatic.AutoTranslating(),"stopping captions left automatic translation active");automatic.Reset();Check(automatic.Text().find("CURRENT AUTO")==std::string::npos,"automatic result leaked across sessions");
 std::atomic<bool> firstEntered{false},firstRelease{false},oldTranslated{false},newRelease{false};std::atomic<int> sequence{0};
 SessionAi overlap([&](const std::wstring& route,const std::string& body,const std::wstring&){
  if(route==L"/transcribe"){if(++sequence==1){firstEntered=true;while(!firstRelease)std::this_thread::sleep_for(1ms);return SessionAi::Reply{true,"first"};}return SessionAi::Reply{true,"second"};}
  if(body=="[alex] first"){oldTranslated=true;return SessionAi::Reply{true,"old translation"};}
  while(!newRelease)std::this_thread::sleep_for(1ms);return SessionAi::Reply{true,"new translation"};
 });
 overlap.Enable(true);overlap.AutoTranslate("en");overlap.Submit("alex",audio);bool firstReady=Wait([&]{return firstEntered.load();});overlap.Submit("alex",audio);firstRelease=true;
 bool oldReady=Wait([&]{return oldTranslated.load();});std::this_thread::sleep_for(20ms);auto during=overlap.CaptionFor("alex");newRelease=true;
 Check(firstReady&&oldReady&&during.original=="second"&&during.translated.empty(),"old translation attached to newer caption");Check(Wait([&]{return overlap.CaptionFor("alex").translated=="new translation";}),"latest translation missing");
 SessionAi silence([](const std::wstring&,const std::string&,const std::wstring&){return SessionAi::Reply{true,"   \n"};});
 silence.Enable(true);silence.Submit("silent",audio);Check(Wait([&]{return silence.Status()==AiMessage::Transcribing;}),"silence not handled");Check(silence.CaptionFor("silent").original.empty()&&silence.Text().find("[silent]")==std::string::npos,"silence produced caption");
 std::cout<<"PASS: default-off, WAV chunks, attributed transcript, summary, keywords, empty states, stop, session fencing and errors\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
