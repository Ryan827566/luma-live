#include "PreviewCall.hpp"
#include "PreviewMedia.hpp"
#include "TcpSignalingServer.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <cmath>
#include <cstring>
#include <windows.h>
#include <objbase.h>
using namespace luma::client::ui::preview;
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    int result=1;
    {
        luma::server::signaling::TcpSignalingServer server;
        uint16_t port=19000;while(port<19100&&!server.Start(port))++port;
        if(port==19100){std::cerr<<"No test signaling port\n";return 2;}
        PreviewCall a,b;std::atomic<int> av{0},bv{0},aa{0},ba{0},loudA{0},loudB{0};
        luma::client::webrtc::WebRtcCallbacks ca,cb;
        ca.on_remote_video=[&](auto f){if(ToBgra(f))++av;};cb.on_remote_video=[&](auto f){if(ToBgra(f))++bv;};
        ca.on_remote_audio=[&](auto f){++aa;if(Peak(f)>.02f)++loudA;};cb.on_remote_audio=[&](auto f){++ba;if(Peak(f)>.02f)++loudB;};
        if(!a.Start("127.0.0.1",port,"rtc-test","a",ca)||!b.Start("127.0.0.1",port,"rtc-test","b",cb)){std::cerr<<"RTC startup failed\n";return 3;}
        VideoFrame video;video.width=320;video.height=240;video.format=PixelFormat::I420;video.data.resize(320*240*3/2,128);
        AudioFrame audio;audio.channels=1;audio.sample_rate=48000;audio.format=AudioSampleFormat::S16;audio.data.resize(960);
        auto start=std::chrono::steady_clock::now();int tick=0;
        while(std::chrono::steady_clock::now()-start<std::chrono::seconds(15)){
            for(auto* call:{&a,&b}){auto status=call->Poll();if(!status.empty())std::cout<<status<<std::endl;}
            if(tick%3==0){video.timestamp_us=static_cast<uint64_t>(tick)*10000;std::fill(video.data.begin(),video.data.begin()+320*240,static_cast<uint8_t>(32+tick%160));a.Video(video);b.Video(video);}
            for(int i=0;i<480;++i){int16_t s=static_cast<int16_t>(std::sin((tick*480+i)*440.*6.283185307/48000)*10000);std::memcpy(audio.data.data()+i*2,&s,2);}
            a.Audio(audio);b.Audio(audio);++tick;
            if(av>15&&bv>15&&loudA>25&&loudB>25){result=0;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout<<"Decoded video A/B="<<av<<'/'<<bv<<", audio="<<aa<<'/'<<ba<<", non-silent="<<loudA<<'/'<<loudB<<std::endl;
        a.Stop();b.Stop();server.Stop();
    }
    CoUninitialize();return result;
}
