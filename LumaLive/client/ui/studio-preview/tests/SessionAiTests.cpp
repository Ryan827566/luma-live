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
 Check(ai.ArchiveText().empty(),"empty session produced an archive");
 ai.Submit("alex",audio);std::this_thread::sleep_for(20ms);Check(calls==0,"default-off uploaded audio");
 Check(!ai.Keywords()&&ai.Status()==AiMessage::NoTranscript,"empty keywords lacks guidance");
 Check(!ai.Speak()&&ai.Status()==AiMessage::NoSummary,"missing summary lacks guidance");
 Check(!ai.Summarize(),"empty summary accepted");ai.Enable(true);ai.Submit("alex",audio);
 Check(Wait([&]{return ai.Text().find("[alex] Project update")!=std::string::npos;}),"transcript missing");
 Check(ai.Summarize(),"summary rejected");Check(Wait([&]{return ai.Text().find("Action: review")!=std::string::npos;}),"summary missing");
 Check(ai.Keywords(),"keywords rejected");Check(Wait([&]{return ai.Text().find("Project, release")!=std::string::npos;}),"keywords missing");
 auto archive=ai.ArchiveText();Check(archive.find("[alex] Project update")!=std::string::npos&&archive.find("Action: review")!=std::string::npos&&archive.find("Project, release")!=std::string::npos,"archive omitted generated content");Check(archive.find(AiMessageText(ai.Status()))==std::string::npos,"archive retained transient UI status");
 ai.Enable(false);Check(ai.ArchiveText()==archive,"stopping captions changed archive content");int before=calls;ai.Submit("alex",audio);std::this_thread::sleep_for(20ms);Check(calls==before,"stop uploaded audio");
 ai.Reset();Check(ai.ArchiveText().empty(),"new session retained previous archive content");Check(ai.Text().find("Project update")==std::string::npos&&ai.Text().find("Project, release")==std::string::npos,"new call retained transcript");
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

 // A canceled in-flight voice response must not play or unlock a newer text job.
 std::atomic<bool> voiceEntered{false},voiceRelease{false},keywordEntered{false},keywordRelease{false};
 std::atomic<int> voiceCalls{0};
 auto gate=[](std::atomic<bool>& released){auto deadline=std::chrono::steady_clock::now()+5s;while(!released&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);};
 SessionAi cancelVoice([&](const std::wstring& route,const std::string&,const std::wstring&){
  if(route==L"/transcribe")return SessionAi::Reply{true,"spoken words"};
  if(route==L"/summary")return SessionAi::Reply{true,"voice summary"};
  if(route==L"/keywords"){keywordEntered=true;gate(keywordRelease);return SessionAi::Reply{true,"voice keyword"};}
  Check(route==L"/speech","unexpected voice-test route");
  if(++voiceCalls==1){voiceEntered=true;gate(voiceRelease);return SessionAi::Reply{true,"CANCELED VOICE"};}
  return SessionAi::Reply{true,"CURRENT VOICE"};
 });
 cancelVoice.Enable(true);cancelVoice.Submit("alex",audio);
 Check(Wait([&]{return cancelVoice.Status()==AiMessage::Transcribing;}),"voice test transcript missing");
 Check(cancelVoice.Summarize()&&Wait([&]{return cancelVoice.Status()==AiMessage::SummaryReady;}),"voice test summary missing");
 Check(cancelVoice.Speak()&&Wait([&]{return voiceEntered.load();}),"voice request did not start");
 cancelVoice.StopSpeech();Check(cancelVoice.Status()==AiMessage::VoiceStopped,"voice cancellation status missing");
 Check(cancelVoice.Keywords(),"canceling voice did not release text-action busy state");
 voiceRelease=true;Check(Wait([&]{return keywordEntered.load();}),"text job did not follow canceled voice");
 Check(cancelVoice.TakeSpeech().empty(),"canceled in-flight voice leaked audio");
 cancelVoice.StopSpeech();Check(cancelVoice.Status()==AiMessage::KeywordsPending,"stopping voice overwrote unrelated pending status");
 Check(!cancelVoice.Summarize(),"late voice response or repeated stop unlocked newer text job");
 keywordRelease=true;Check(Wait([&]{return cancelVoice.Status()==AiMessage::KeywordsReady;}),"newer text action failed");
 Check(cancelVoice.Speak()&&Wait([&]{return cancelVoice.Status()==AiMessage::VoiceReady;}),"new voice rejected after cancellation");
 cancelVoice.StopSpeech();Check(cancelVoice.TakeSpeech().empty()&&cancelVoice.Status()==AiMessage::VoiceStopped,"canceling ready voice retained audio");
 Check(cancelVoice.Speak()&&Wait([&]{return cancelVoice.Status()==AiMessage::VoiceReady;}),"second new voice failed");
 auto packet=cancelVoice.TakeSpeechPacket();Check(packet.audio=="CURRENT VOICE"&&voiceCalls==3,"new voice epoch failed");
 cancelVoice.Reset();auto resetPacket=cancelVoice.TakeSpeechPacket();Check(resetPacket.audio.empty()&&resetPacket.revision!=packet.revision,"session reset did not invalidate playing speech");
 auto revision=resetPacket.revision;cancelVoice.Enable(true);auto enabledPacket=cancelVoice.TakeSpeechPacket();Check(enabledPacket.audio.empty()&&enabledPacket.revision!=revision,"capture restart retained playback revision");

 // Queued voice jobs must be removed before the provider sees them.
 std::atomic<int> queuedAsrCalls{0},queuedVoiceCalls{0};std::atomic<bool> queuedAsrEntered{false},queuedAsrRelease{false};
 SessionAi queuedVoice([&](const std::wstring& route,const std::string&,const std::wstring&){
  if(route==L"/transcribe"){if(++queuedAsrCalls==2){queuedAsrEntered=true;gate(queuedAsrRelease);}return SessionAi::Reply{true,"queued voice words"};}
  if(route==L"/speech"){++queuedVoiceCalls;return SessionAi::Reply{true,"UNEXPECTED VOICE"};}
  return SessionAi::Reply{true,"queued voice summary"};
 });
 queuedVoice.Enable(true);queuedVoice.Submit("alex",audio);
 Check(Wait([&]{return queuedVoice.Status()==AiMessage::Transcribing;}),"queued voice transcript missing");
 Check(queuedVoice.Summarize()&&Wait([&]{return queuedVoice.Status()==AiMessage::SummaryReady;}),"queued voice summary missing");
 queuedVoice.Submit("alex",audio);Check(Wait([&]{return queuedAsrEntered.load();}),"queued voice blocker missing");
 Check(queuedVoice.Speak(),"voice job failed to queue");queuedVoice.StopSpeech();queuedAsrRelease=true;
 Check(queuedVoice.Summarize()&&Wait([&]{return queuedVoice.Status()==AiMessage::SummaryReady;}),"queued cancellation left actions busy");
 Check(queuedVoiceCalls==0&&queuedVoice.TakeSpeech().empty(),"canceled queued voice reached provider");
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
 std::cout<<"PASS: default-off, WAV chunks, attributed transcript, summary, keywords, empty states, stop, speech cancellation, session fencing and errors\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
