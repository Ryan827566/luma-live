#pragma once
#include "MediaTypes.hpp"
#include "AiMessages.hpp"
#include <windows.h>
#include <winhttp.h>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace luma::client::ui::preview {
// Audio never leaves this process until the user explicitly enables transcription.
class SessionAi {
public:
 struct Caption {std::string original,translated;};
 Caption CaptionFor(const std::string& speaker,std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now()){
  std::lock_guard lock(mutex_);auto found=captions_.find(speaker);
  if(!enabled_||found==captions_.end()||now-found->second.updated>=std::chrono::seconds(10))return {};
  return found->second.text;
 }
 struct Reply { bool ok; std::string text; };
 using Transport=std::function<Reply(const std::wstring&,const std::string&,const std::wstring&)>;
 explicit SessionAi(Transport transport=Http):transport_(std::move(transport)),worker_([this]{Run();}){}
 ~SessionAi(){{std::lock_guard lock(mutex_);quit_=true;enabled_=false;jobs_.clear();}wake_.notify_one();worker_.join();}
 void Enable(bool on){std::lock_guard lock(mutex_);enabled_=on;++generation_;if(!on){autoLanguage_.clear();++autoEpoch_;}buffers_.clear();jobs_.clear();speech_.clear();captions_.clear();summaryPending_=false;speechPending_=false;++speechEpoch_;error_.clear();status_=on?AiMessage::Enabled:AiMessage::Stopped;}
 void Reset(){Enable(false);std::lock_guard lock(mutex_);transcript_.clear();summary_.clear();translation_.clear();keywords_.clear();autoTranslation_.clear();}
 bool Enabled(){std::lock_guard lock(mutex_);return enabled_;}
 void Submit(const std::string& speaker,const media::pipeline::AudioFrame& frame){
  std::lock_guard lock(mutex_);if(!enabled_)return;
  if(frame.sample_rate!=48000||frame.channels!=1||frame.format!=media::pipeline::AudioSampleFormat::S16||frame.data.size()%2){status_=AiMessage::AudioFormat;return;}
  if(frame.data.size()>480000||(!buffers_.count(speaker)&&buffers_.size()>=6))return;
  auto& pcm=buffers_[speaker];pcm.append(reinterpret_cast<const char*>(frame.data.data()),frame.data.size());
  if(pcm.size()>=480000){if(jobs_.size()<8){jobs_.push_back({generation_,false,speaker,Wave(pcm.substr(0,480000))});wake_.notify_one();}else status_=AiMessage::Dropped;pcm.erase(0,480000);}
 }
 bool Translate(const std::string& language){std::lock_guard lock(mutex_);if(language!="zh"&&language!="en"&&language!="ja"&&language!="ko"&&language!="es")return false;if(!ReadyForText())return false;summaryPending_=true;Job job{generation_,true,"",transcript_};job.language=language;jobs_.push_back(std::move(job));status_=AiMessage::Translating;wake_.notify_one();return true;}
 bool Speak(){std::lock_guard lock(mutex_);if(summary_.empty()){status_=AiMessage::NoSummary;return false;}if(summaryPending_||jobs_.size()>=8){status_=AiMessage::Busy;return false;}if(std::count_if(summary_.begin(),summary_.end(),[](unsigned char c){return (c&0xc0)!=0x80;})>4000){status_=AiMessage::SpeechLimit;return false;}Job job{generation_,true,"",summary_};job.speech=true;job.speechEpoch=speechEpoch_;summaryPending_=true;speechPending_=true;jobs_.push_back(std::move(job));status_=AiMessage::VoicePending;wake_.notify_one();return true;}
 // Cancel queued and in-flight results; the underlying HTTP operation may finish later.
 void StopSpeech(){std::lock_guard lock(mutex_);const bool ownedBusy=speechPending_;const bool hadSpeech=ownedBusy||!speech_.empty()||status_==AiMessage::VoiceReady; ++speechEpoch_;std::erase_if(jobs_,[](const Job& job){return job.speech;});speech_.clear();speechPending_=false;if(ownedBusy)summaryPending_=false;if(hadSpeech&&!summaryPending_){status_=AiMessage::VoiceStopped;error_.clear();}}
 bool AutoTranslating(){std::lock_guard lock(mutex_);return !autoLanguage_.empty();}
 bool AutoTranslate(const std::string& language){std::lock_guard lock(mutex_);if(!language.empty()&&language!="en"&&language!="zh"&&language!="ja"&&language!="ko"&&language!="es")return false;if(!language.empty()&&!enabled_){status_=AiMessage::NoTranscript;return false;}if(language!=autoLanguage_&&!language.empty())autoTranslation_.clear();autoLanguage_=language;++autoEpoch_;for(auto& entry:captions_)entry.second.text.translated.clear();std::erase_if(jobs_,[](const Job& job){return job.autoEpoch!=0;});return true;}
 bool Keywords(){std::lock_guard lock(mutex_);if(!ReadyForText())return false;Job job{generation_,true,"",transcript_};job.keywords=true;summaryPending_=true;jobs_.push_back(std::move(job));status_=AiMessage::KeywordsPending;wake_.notify_one();return true;}
 std::string LastError(){std::lock_guard lock(mutex_);return error_;}
 AiMessage Status(){std::lock_guard lock(mutex_);return status_;}
 std::string TakeSpeech(){std::lock_guard lock(mutex_);return std::exchange(speech_,{});}
 bool Summarize(){std::lock_guard lock(mutex_);if(!ReadyForText())return false;summaryPending_=true;jobs_.push_back({generation_,true,"",transcript_});status_=AiMessage::SummaryPending;wake_.notify_one();return true;}
 std::string Text(){std::lock_guard lock(mutex_);return AiMessageText(status_)+"\r\n\r\n"+transcript_+(summary_.empty()?"":"\r\n"+UiUtf8(UiLabel(L"\u6458\u8981\u4e0e\u5f85\u529e",L"Summary and actions"))+"\r\n"+summary_)+(translation_.empty()?"":"\r\n"+UiUtf8(UiLabel(L"\u7ffb\u8bd1",L"Translation"))+"\r\n"+translation_)+(keywords_.empty()?"":"\r\n"+UiUtf8(UiLabel(L"\u5173\u952e\u8bcd",L"Keywords"))+"\r\n"+keywords_)+(autoTranslation_.empty()?"":"\r\n"+UiUtf8(UiLabel(L"\u81ea\u52a8\u7ffb\u8bd1",L"Automatic translation"))+"\r\n"+autoTranslation_);}

private:
 struct CaptionEntry {Caption text;std::chrono::steady_clock::time_point updated;std::uint64_t serial;};
 std::map<std::string,CaptionEntry> captions_;std::uint64_t captionSerial_=0;
 bool ReadyForText(){if(transcript_.empty()){status_=AiMessage::NoTranscript;return false;}if(summaryPending_||jobs_.size()>=8){status_=AiMessage::Busy;return false;}return true;}
 struct Job {std::uint64_t generation;bool summary;std::string speaker,body;std::string language;bool speech=false;bool keywords=false;std::uint64_t autoEpoch=0,captionSerial=0,speechEpoch=0;};
 struct Handle{HINTERNET h{};~Handle(){if(h)WinHttpCloseHandle(h);}operator HINTERNET()const{return h;}};
 static Reply Http(const std::wstring& path,const std::string& body,const std::wstring& contentType){
  Handle session{WinHttpOpen(L"LumaLive/1",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0)};
  if(!session.h)return {false,"Unable to initialize AI HTTP client."};
  WinHttpSetTimeouts(session,2000,2000,5000,15000);
  INTERNET_PORT port=19740;wchar_t configuredPort[16]{};auto length=GetEnvironmentVariableW(L"LUMALIVE_AI_GATEWAY_PORT",configuredPort,16);
  if(length){if(length>=16)return {false,"Invalid AI gateway port."};wchar_t* end=nullptr;auto value=wcstoul(configuredPort,&end,10);if(!value||value>65535||*end)return {false,"Invalid AI gateway port."};port=INTERNET_PORT(value);}
  Handle connection{WinHttpConnect(session,L"127.0.0.1",port,0)};
  Handle request{connection.h?WinHttpOpenRequest(connection,L"POST",path.substr(0,path.find(L'?')).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0):nullptr};
  if(!request.h)return {false,"Unable to connect to local AI gateway."};
  DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
  auto headers=L"Content-Type: "+contentType+L"\r\n";
  auto route=path;auto marker=route.find(L'?');if(marker!=std::wstring::npos){auto lang=route.substr(marker+1);headers+=L"X-Luma-Language: "+lang+L"\r\n";}
  if(!WinHttpSendRequest(request,headers.c_str(),DWORD(-1),const_cast<char*>(body.data()),DWORD(body.size()),DWORD(body.size()),0)||!WinHttpReceiveResponse(request,nullptr))return {false,"AI gateway unavailable or timed out. Start the local gateway and configure its provider."};
  DWORD code=0,size=sizeof(code);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&size,nullptr))return {false,"Invalid AI response."};
  std::string result;char buffer[4096];DWORD read=0;
  do{if(!WinHttpReadData(request,buffer,sizeof(buffer),&read))return {false,"AI response interrupted."};if(result.size()+read>(path==L"/speech"?4194304u:131072u))return {false,"AI response too large."};result.append(buffer,read);}while(read);
  if(result.empty()&&path!=L"/transcribe")return {false,"AI returned an empty response."};
  return {code>=200&&code<300,result};
 }
 static std::string Wave(const std::string& pcm){
  std::string wav;auto word=[&](unsigned value,int bytes){for(int i=0;i<bytes;++i)wav.push_back(char((value>>(8*i))&255));};
  wav="RIFF";word(unsigned(pcm.size()+36),4);wav+="WAVEfmt ";word(16,4);word(1,2);word(1,2);word(48000,4);word(96000,4);word(2,2);word(16,2);wav+="data";word(unsigned(pcm.size()),4);wav+=pcm;return wav;
 }
 void Run(){for(;;){Job job;{std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return quit_||!jobs_.empty();});if(quit_)return;job=std::move(jobs_.front());jobs_.pop_front();if((job.autoEpoch&&job.autoEpoch!=autoEpoch_)||(job.speech&&job.speechEpoch!=speechEpoch_))continue;}
   Reply result;try{result=transport_((job.speech?L"/speech":job.keywords?L"/keywords":!job.language.empty()?L"/translate?"+std::wstring(job.language.begin(),job.language.end()):job.summary?L"/summary":L"/transcribe"),job.body,job.summary?L"text/plain; charset=utf-8":L"audio/wav");}catch(...){result={false,"AI request failed."};}
   std::lock_guard lock(mutex_);if(quit_||job.generation!=generation_||(job.autoEpoch&&job.autoEpoch!=autoEpoch_)||(job.speech&&job.speechEpoch!=speechEpoch_))continue;
   if(job.summary&&!job.autoEpoch)summaryPending_=false;if(job.speech)speechPending_=false;
   if(!result.ok){error_=result.text;status_=AiMessage::RequestFailed;continue;}
   error_.clear();
   if(job.autoEpoch){auto caption=captions_.find(job.speaker);if(caption!=captions_.end()&&caption->second.serial==job.captionSerial)caption->second.text.translated=result.text;
    if(autoTranslation_.size()+result.text.size()+2<=64000)autoTranslation_+=result.text+"\r\n";else status_=AiMessage::Dropped;}
   else if(job.speech){speech_=std::move(result.text);status_=AiMessage::VoiceReady;}
   else if(job.keywords){keywords_=result.text;status_=AiMessage::KeywordsReady;}
   else if(!job.language.empty()){translation_=result.text;status_=AiMessage::TranslationReady;}
   else if(job.summary){summary_=result.text;status_=AiMessage::SummaryReady;}
   else if(result.text.find_first_not_of(" \r\n\t")==std::string::npos){captions_.erase(job.speaker);status_=AiMessage::Transcribing;}
   else if(transcript_.size()+job.speaker.size()+result.text.size()+6<=64000){transcript_+="["+job.speaker+"] "+result.text+"\r\n";captions_[job.speaker]={{result.text,{}},std::chrono::steady_clock::now(),++captionSerial_};status_=AiMessage::Transcribing;if(!autoLanguage_.empty()){if(jobs_.size()<8){Job translation{generation_,true,job.speaker,"["+job.speaker+"] "+result.text};translation.captionSerial=captionSerial_;translation.language=autoLanguage_;translation.autoEpoch=autoEpoch_;jobs_.push_back(std::move(translation));wake_.notify_one();}else status_=AiMessage::Dropped;}}
   else{enabled_=false;autoLanguage_.clear();++autoEpoch_;buffers_.clear();jobs_.clear();summaryPending_=false;speechPending_=false;++speechEpoch_;status_=AiMessage::Limit;}
  }}
 Transport transport_;std::mutex mutex_;std::condition_variable wake_;std::deque<Job> jobs_;std::map<std::string,std::string> buffers_;
 bool quit_=false,enabled_=false,summaryPending_=false,speechPending_=false;std::uint64_t generation_=0,autoEpoch_=1,speechEpoch_=1;std::string autoLanguage_,autoTranslation_;std::string transcript_,summary_,translation_,speech_,keywords_,error_;AiMessage status_=AiMessage::Off;std::thread worker_;
};
}
