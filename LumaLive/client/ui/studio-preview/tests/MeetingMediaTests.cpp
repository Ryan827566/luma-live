#include "MeetingMedia.hpp"
#include "TcpSignalingServer.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <objbase.h>
using Media=luma::client::ui::preview::MeetingMedia;
using Session=luma::client::ui::preview::MeetingSession;
using namespace std::chrono_literals;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){CoInitializeEx(nullptr,COINIT_MULTITHREADED);int result=0;try{
 luma::server::signaling::TcpSignalingServer server;unsigned short port=19600;while(port<19700&&!server.Start(port))++port;require(port<19700,"server start");
 std::array<std::array<std::atomic<int>,3>,3> videos{},audio{};std::array<Media,3> clients;std::array<int,3> mixed{};
 auto start=[&](int index,bool create){Media::Callbacks cb;cb.video=[&,index](const std::string& id,Media::Video frame){int from=id[0]-'a';if(from>=0&&from<3&&frame.width==160&&frame.height==120)++videos[index][from];};cb.audio=[&,index](const std::string& id,Media::Audio frame){int from=id[0]-'a';if(from<0||from>=3)return;for(size_t n=0;n+1<frame.data.size();n+=2){int16_t sample;std::memcpy(&sample,frame.data.data()+n,2);if(std::abs(int(sample))>100){++audio[index][from];break;}}};return clients[index].Start("127.0.0.1",port,"media-test",std::string(1,char('a'+index)),create,cb,{});};
 require(start(0,true),"host start");auto deadline=std::chrono::steady_clock::now()+3s;while(!clients[0].Session().IsHost()&&std::chrono::steady_clock::now()<deadline){clients[0].Poll();std::this_thread::sleep_for(5ms);}require(clients[0].Session().IsHost(),"host registration");require(start(1,false)&&start(2,false),"join peers");
 Media::Video frame;frame.width=160;frame.height=120;frame.format=luma::client::media::pipeline::PixelFormat::I420;frame.data.resize(28800,128);
 Media::Audio pcm;pcm.sample_rate=48000;pcm.channels=1;pcm.format=luma::client::media::pipeline::AudioSampleFormat::S16;pcm.data.resize(960);
 int tick=0;auto pump=[&]{for(int i=0;i<3;++i){clients[i].Poll();require(clients[i].Error().empty(),clients[i].Error().c_str());if(clients[i].Session().GetState()==Session::State::Joined){clients[i].SetVideo("camera");clients[i].SetMicrophone(true);frame.timestamp_us=std::uint64_t(tick)*10000;std::fill(frame.data.begin(),frame.data.begin()+19200,static_cast<unsigned char>(40+i*60));if(tick%3==0)clients[i].VideoFrame(frame);for(int n=0;n<480;++n){int16_t sample=static_cast<int16_t>(std::sin((tick*480+n)*(421.+i*313)*6.283185307/48000)*12000);std::memcpy(pcm.data.data()+n*2,&sample,2);}clients[i].AudioFrame(pcm);auto output=clients[i].MixAudio();for(size_t n=0;n+1<output.data.size();n+=2){int16_t value;std::memcpy(&value,output.data.data()+n,2);if(std::abs(int(value))>100){++mixed[i];break;}}}}++tick;std::this_thread::sleep_for(10ms);};
 auto complete=[&]{for(int to=0;to<3;++to)for(int from=0;from<3;++from)if(to!=from&&(videos[to][from]<5||audio[to][from]<8))return false;return true;};
 deadline=std::chrono::steady_clock::now()+20s;while(!complete()&&std::chrono::steady_clock::now()<deadline)pump();
 for(int to=0;to<3;++to)for(int from=0;from<3;++from)if(to!=from)std::cout<<from<<"->"<<to<<" video="<<videos[to][from]<<" audible="<<audio[to][from]<<'\n';require(complete(),"Not all six directed media paths decoded");for(auto blocks:mixed)require(blocks>0,"Decoded audio did not reach meeting mixer");
 require(clients[0].Session().Remove("b"),"remove member");deadline=std::chrono::steady_clock::now()+3s;while(clients[1].Session().GetState()!=Session::State::Removed&&std::chrono::steady_clock::now()<deadline)pump();require(clients[1].Session().GetState()==Session::State::Removed,"member was not removed");
 int before=videos[1][0]+videos[1][2]+audio[1][0]+audio[1][2];for(int n=0;n<20;++n)pump();require(before==videos[1][0]+videos[1][2]+audio[1][0]+audio[1][2],"removed client still received media");
 require(clients[0].Session().End(),"end meeting");deadline=std::chrono::steady_clock::now()+3s;while(clients[2].Session().GetState()!=Session::State::Ended&&std::chrono::steady_clock::now()<deadline)pump();require(clients[2].Session().GetState()==Session::State::Ended,"meeting did not end");for(auto& client:clients)client.Leave();server.Stop();std::cout<<"PASS: three-client decoded video/audio mesh, removal and meeting end\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
