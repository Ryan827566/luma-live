#include "SessionAi.hpp"
#include <chrono>
#include <iostream>
using namespace luma::client::ui::preview;
bool Wait(SessionAi& ai,const char* text){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(std::chrono::steady_clock::now()<end){if(ai.Text().find(text)!=std::string::npos)return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}std::cerr<<ai.Text()<<'\n';return false;}
int main(){SessionAi ai;ai.Enable(true);luma::client::media::pipeline::AudioFrame audio;audio.sample_rate=48000;audio.channels=1;audio.format=luma::client::media::pipeline::AudioSampleFormat::S16;audio.data.resize(480000,0);ai.Submit("speaker",audio);
 if(!Wait(ai,"[speaker] HTTP transcript"))return 1;
 if(!ai.Summarize()||!Wait(ai,"HTTP summary"))return 2;
 if(!ai.Translate("ja")||!Wait(ai,"HTTP translation"))return 3;
 if(ai.Translate("unsupported"))return 4;
 if(!ai.Speak())return 5;
 std::string speech;auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(speech.empty()&&std::chrono::steady_clock::now()<end){speech=ai.TakeSpeech();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
 if(speech.size()!=4844||speech.substr(0,4)!="RIFF")return 6;
 ai.Enable(false);std::cout<<"PASS: native WinHTTP -> local gateway -> mock model: ASR, summary, translation, TTS WAV\n";return 0;
}
