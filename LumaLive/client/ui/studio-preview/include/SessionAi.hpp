#pragma once
#include "MediaTypes.hpp"
#include <windows.h>
#include <winhttp.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace luma::client::ui::preview {
// Audio never leaves this process until the user explicitly enables transcription.
class SessionAi {
public:
 struct Reply { bool ok; std::string text; };
 using Transport=std::function<Reply(const std::wstring&,const std::string&,const std::wstring&)>;
 explicit SessionAi(Transport transport=Http):transport_(std::move(transport)),worker_([this]{Run();}){}
 ~SessionAi(){{std::lock_guard lock(mutex_);quit_=true;enabled_=false;jobs_.clear();}wake_.notify_one();worker_.join();}
 void Enable(bool on){std::lock_guard lock(mutex_);enabled_=on;++generation_;buffers_.clear();jobs_.clear();summaryPending_=false;status_=on?"Transcription enabled; audio is sent to the configured service.":"Transcription stopped.";}
 void Reset(){Enable(false);std::lock_guard lock(mutex_);transcript_.clear();summary_.clear();}
 bool Enabled(){std::lock_guard lock(mutex_);return enabled_;}
 void Submit(const std::string& speaker,const media::pipeline::AudioFrame& frame){
  std::lock_guard lock(mutex_);if(!enabled_)return;
  if(frame.sample_rate!=48000||frame.channels!=1||frame.format!=media::pipeline::AudioSampleFormat::S16||frame.data.size()%2){status_="AI requires 48 kHz mono S16 audio.";return;}
  if(frame.data.size()>480000||(!buffers_.count(speaker)&&buffers_.size()>=6))return;
  auto& pcm=buffers_[speaker];pcm.append(reinterpret_cast<const char*>(frame.data.data()),frame.data.size());
  if(pcm.size()>=480000){if(jobs_.size()<8){jobs_.push_back({generation_,false,speaker,Wave(pcm.substr(0,480000))});wake_.notify_one();}else status_="AI is busy; an audio segment was dropped.";pcm.erase(0,480000);}
 }
 bool Summarize(){std::lock_guard lock(mutex_);if(transcript_.empty()||summaryPending_||jobs_.size()>=8)return false;summaryPending_=true;jobs_.push_back({generation_,true,"",transcript_});status_="Generating summary...";wake_.notify_one();return true;}
 std::string Text(){std::lock_guard lock(mutex_);return status_+"\r\n\r\n"+transcript_+(summary_.empty()?"":"\r\n--- AI summary ---\r\n"+summary_);}
private:
 struct Job {std::uint64_t generation;bool summary;std::string speaker,body;};
 struct Handle{HINTERNET h{};~Handle(){if(h)WinHttpCloseHandle(h);}operator HINTERNET()const{return h;}};
 static Reply Http(const std::wstring& path,const std::string& body,const std::wstring& contentType){
  Handle session{WinHttpOpen(L"LumaLive/1",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0)};
  if(!session.h)return {false,"Unable to initialize AI HTTP client."};
  WinHttpSetTimeouts(session,2000,2000,5000,15000);
  Handle connection{WinHttpConnect(session,L"127.0.0.1",19740,0)};
  Handle request{connection.h?WinHttpOpenRequest(connection,L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0):nullptr};
  if(!request.h)return {false,"Unable to connect to local AI gateway."};
  DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
  auto headers=L"Content-Type: "+contentType+L"\r\n";
  if(!WinHttpSendRequest(request,headers.c_str(),DWORD(-1),const_cast<char*>(body.data()),DWORD(body.size()),DWORD(body.size()),0)||!WinHttpReceiveResponse(request,nullptr))return {false,"AI gateway unavailable or timed out. Start the local gateway and configure its provider."};
  DWORD code=0,size=sizeof(code);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&size,nullptr))return {false,"Invalid AI response."};
  std::string result;char buffer[4096];DWORD read=0;
  do{if(!WinHttpReadData(request,buffer,sizeof(buffer),&read))return {false,"AI response interrupted."};if(result.size()+read>131072)return {false,"AI response too large."};result.append(buffer,read);}while(read);
  return {code>=200&&code<300,result.empty()?"AI returned an empty response.":result};
 }
 static std::string Wave(const std::string& pcm){
  std::string wav;auto word=[&](unsigned value,int bytes){for(int i=0;i<bytes;++i)wav.push_back(char((value>>(8*i))&255));};
  wav="RIFF";word(unsigned(pcm.size()+36),4);wav+="WAVEfmt ";word(16,4);word(1,2);word(1,2);word(48000,4);word(96000,4);word(2,2);word(16,2);wav+="data";word(unsigned(pcm.size()),4);wav+=pcm;return wav;
 }
 void Run(){for(;;){Job job;{std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return quit_||!jobs_.empty();});if(quit_)return;job=std::move(jobs_.front());jobs_.pop_front();}
   Reply result;try{result=transport_(job.summary?L"/summary":L"/transcribe",job.body,job.summary?L"text/plain; charset=utf-8":L"audio/wav");}catch(...){result={false,"AI request failed."};}
   std::lock_guard lock(mutex_);if(quit_||job.generation!=generation_)continue;
   if(job.summary)summaryPending_=false;
   if(!result.ok){status_=result.text;continue;}
   if(job.summary){summary_=result.text;status_="Summary ready. Verify AI output against the transcript.";}
   else if(transcript_.size()+job.speaker.size()+result.text.size()+6<=64000){transcript_+="["+job.speaker+"] "+result.text+"\r\n";status_="Transcribing (five-second segments)...";}
   else{enabled_=false;buffers_.clear();jobs_.clear();status_="Transcript limit reached. Copy your transcript before starting a new session.";}
  }}
 Transport transport_;std::mutex mutex_;std::condition_variable wake_;std::deque<Job> jobs_;std::map<std::string,std::string> buffers_;
 bool quit_=false,enabled_=false,summaryPending_=false;std::uint64_t generation_=0;std::string transcript_,summary_,status_="AI is off. Configure and start the local AI gateway before enabling.";std::thread worker_;
};
}
