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
  Check(path==L"/summary"&&body.find("[alex] Project update")!=std::string::npos,"summary missing attributed transcript");return SessionAi::Reply{true,"Action: review the release"};
 });
 luma::client::media::pipeline::AudioFrame audio;audio.sample_rate=48000;audio.channels=1;audio.format=luma::client::media::pipeline::AudioSampleFormat::S16;audio.data.resize(480000,0);
 ai.Submit("alex",audio);std::this_thread::sleep_for(20ms);Check(calls==0,"default-off uploaded audio");
 Check(!ai.Summarize(),"empty summary accepted");ai.Enable(true);ai.Submit("alex",audio);
 Check(Wait([&]{return ai.Text().find("[alex] Project update")!=std::string::npos;}),"transcript missing");
 Check(ai.Summarize(),"summary rejected");Check(Wait([&]{return ai.Text().find("Action: review")!=std::string::npos;}),"summary missing");
 ai.Enable(false);int before=calls;ai.Submit("alex",audio);std::this_thread::sleep_for(20ms);Check(calls==before,"stop uploaded audio");
 ai.Reset();Check(ai.Text().find("Project update")==std::string::npos,"new call retained transcript");
 std::atomic<bool> entered{false},release{false};
 SessionAi stale([&](const std::wstring&,const std::string&,const std::wstring&){entered=true;while(!release)std::this_thread::sleep_for(1ms);return SessionAi::Reply{true,"OLD SESSION"};});
 stale.Enable(true);stale.Submit("old",audio);bool started=Wait([&]{return entered.load();});stale.Reset();release=true;Check(started,"worker did not start");
 std::this_thread::sleep_for(30ms);Check(stale.Text().find("OLD SESSION")==std::string::npos,"late result contaminated new session");
 SessionAi failing([](const std::wstring&,const std::string&,const std::wstring&){return SessionAi::Reply{false,"provider not configured"};});
 failing.Enable(true);failing.Submit("alex",audio);Check(Wait([&]{return failing.Text().find("provider not configured")!=std::string::npos;}),"provider error hidden");
 std::cout<<"PASS: default-off, WAV chunks, attributed transcript, summary, stop, session fencing and errors\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
