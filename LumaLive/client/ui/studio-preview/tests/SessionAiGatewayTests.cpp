#include "SessionAi.hpp"
#include <chrono>
#include <iostream>
using namespace luma::client::ui::preview;
bool Wait(SessionAi& ai,const char* text){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(std::chrono::steady_clock::now()<end){if(ai.Text().find(text)!=std::string::npos)return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}std::cerr<<ai.Text()<<'\n';return false;}
int ResponseBoundary(const std::string& route){
 SessionAi ai;ai.Enable(true);
 luma::client::media::pipeline::AudioFrame audio;audio.sample_rate=48000;audio.channels=1;audio.format=luma::client::media::pipeline::AudioSampleFormat::S16;audio.data.resize(480000,0);
 auto await=[&](auto predicate){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));return predicate();};
 auto ready=route=="/transcribe"?AiMessage::Transcribing:route=="/summary"?AiMessage::SummaryReady:route=="/translate"?AiMessage::TranslationReady:route=="/keywords"?AiMessage::KeywordsReady:AiMessage::VoiceReady;
 auto request=[&]{if(route=="/transcribe"){ai.Submit("speaker",audio);return true;}if(route=="/summary")return ai.Summarize();if(route=="/translate")return ai.Translate("ja");if(route=="/keywords")return ai.Keywords();return ai.Speak();};
 if(route!="/transcribe"){
  ai.Submit("speaker",audio);if(!await([&]{return ai.Status()==AiMessage::Transcribing;}))return 20;
  if(route=="/speech"&&(!ai.Summarize()||!await([&]{return ai.Status()==AiMessage::SummaryReady;})))return 21;
 }
 if(!request()||!await([&]{return ai.Status()==AiMessage::RequestFailed||ai.Status()==ready;}))return 22;
 if(ai.Status()!=AiMessage::RequestFailed||ai.LastError().empty()){std::cerr<<"Incomplete response was accepted\n";return 23;}
 if(ai.ArchiveText().find("PARTIAL")!=std::string::npos||!ai.TakeSpeech().empty())return 24;
 if(!request()||!await([&]{return ai.Status()==ready;}))return 25;
 if(!ai.LastError().empty())return 26;
 if(route=="/speech"&&ai.TakeSpeech().size()!=4844)return 27;
 ai.Enable(false);std::cout<<"PASS: rejected incomplete response and recovered "<<route<<'\n';return 0;
}
int main(int argc,char** argv){if(argc==2)return ResponseBoundary(argv[1]);SessionAi ai;ai.Enable(true);luma::client::media::pipeline::AudioFrame audio;audio.sample_rate=48000;audio.channels=1;audio.format=luma::client::media::pipeline::AudioSampleFormat::S16;audio.data.resize(480000,0);ai.Submit("speaker",audio);
 if(!Wait(ai,"[speaker] HTTP transcript"))return 1;
 if(!ai.Summarize()||!Wait(ai,"HTTP summary"))return 2;
 if(!ai.Translate("ja")||!Wait(ai,"HTTP translation"))return 3;
 if(ai.Translate("unsupported"))return 4;
 if(!ai.Speak())return 5;
 std::string speech;auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(speech.empty()&&std::chrono::steady_clock::now()<end){speech=ai.TakeSpeech();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
 if(speech.size()!=4844||speech.substr(0,4)!="RIFF")return 6;
 if(!ai.Keywords()||!Wait(ai,"HTTP keywords"))return 7;
 ai.Enable(false);std::cout<<"PASS: native WinHTTP -> local gateway -> mock model: ASR, summary, translation, TTS WAV, keywords\n";return 0;
}
