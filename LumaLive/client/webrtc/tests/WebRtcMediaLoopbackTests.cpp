#include "NativeWebRtcPeerConnection.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

int main() {
    std::cerr << "Starting loopback\n";
    using namespace luma::client::webrtc;
    using namespace luma::client::media::pipeline;
    using namespace std::chrono;
    std::mutex mutex;
    std::deque<std::function<void()>> events;
    auto post = [&](std::function<void()> event) { std::lock_guard lock(mutex); events.push_back(std::move(event)); };
    auto a = NativeWebRtcPeerConnection::Create(), b = NativeWebRtcPeerConnection::Create();
    std::atomic<int> videos{0}, audible{0};
    std::atomic<bool> stats_received{false};
    bool aRemote = false, bRemote = false;
    std::deque<std::function<void()>> aIce, bIce;
    WebRtcCallbacks ac, bc;
    ac.on_local_description = [&](auto type, auto sdp) { std::cerr<<"A SDP "<<type<<" bytes="<<sdp.size()<<"\n"; post([&,type,sdp] { b->SetRemoteDescription(type,sdp); }); };
    bc.on_local_description = [&](auto type, auto sdp) { std::cerr<<"B SDP "<<type<<" bytes="<<sdp.size()<<"\n"; post([&,type,sdp] { a->SetRemoteDescription(type,sdp); }); };
    ac.on_remote_description_set = [&] { post([&] { aRemote=true; for(auto& f:aIce)f();aIce.clear(); }); };
    bc.on_remote_description_set = [&] { post([&] { bRemote=true; for(auto& f:bIce)f();bIce.clear(); b->CreateAnswer(); }); };
    ac.on_local_ice_candidate = [&](auto mid,int index,auto candidate) { post([&,mid,index,candidate] { auto f=[=] { b->AddRemoteIceCandidate(mid,index,candidate); }; if(bRemote)f();else bIce.push_back(f); }); };
    bc.on_local_ice_candidate = [&](auto mid,int index,auto candidate) { post([&,mid,index,candidate] { auto f=[=] { a->AddRemoteIceCandidate(mid,index,candidate); }; if(aRemote)f();else aIce.push_back(f); }); };
    bc.on_remote_video = [&](VideoFrame frame) { if(frame.width==160 && frame.height==120 && frame.data.size()==28800) ++videos; };
    bc.on_remote_audio = [&](AudioFrame frame) { for(size_t i=0;i+1<frame.data.size();i+=2) { int16_t sample; std::memcpy(&sample,frame.data.data()+i,2); if(std::abs(int(sample))>100){++audible;break;} } };
    ac.on_connection_state = [](auto state) { std::cout << "A: " << state << '\n'; };
    bc.on_connection_state = [](auto state) { std::cout << "B: " << state << '\n'; };
    std::cerr << "Initializing peer A/B\n";
    if(!a->Initialize({},std::move(ac))||!b->Initialize({},std::move(bc))||!a->CreateOffer()) { std::cerr<<"Initialization failed\n";return 1; }
    VideoFrame video;video.width=160;video.height=120;video.format=PixelFormat::I420;video.data.resize(28800,128);
    std::fill(video.data.begin(),video.data.begin()+19200,90);
    AudioFrame audio;audio.sample_rate=48000;audio.channels=1;audio.format=AudioSampleFormat::S16;audio.data.resize(960);
    const auto start=steady_clock::now();int tick=0;
    while(steady_clock::now()-start<seconds(12) && (videos<5||audible<5||!stats_received)) {
        std::deque<std::function<void()>> ready; {std::lock_guard lock(mutex);ready.swap(events);} for(auto& event:ready) event();
        for(int i=0;i<480;++i){int16_t sample=static_cast<int16_t>(std::sin((tick*480+i)*6.283185307179586*440/48000)*12000);std::memcpy(audio.data.data()+i*2,&sample,2);}
        if(!a->AddAudioFrame(audio)){std::cerr<<"PCM rejected\n";return 2;}
        if(tick%3==0){video.timestamp_us=duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();if(!a->AddVideoFrame(video)){std::cerr<<"Video rejected\n";return 3;}}
        if (tick % 50 == 0 && bRemote) {
            b->GetNetworkStats([&](WebRtcNetworkStats stats) {
                if (stats.report_ready && stats.inbound_ready && stats.jitter_ready &&
                    stats.packets_received > 0 && !stats.selected_candidate_pair_id.empty() &&
                    stats.packet_loss_percent >= 0 && stats.packet_loss_percent <= 100)
                    stats_received = true;
            });
        }
        ++tick;std::this_thread::sleep_for(milliseconds(10));
    }
    a->Close();b->Close();
    std::cout<<"Decoded video frames: "<<videos<<", audible PCM blocks: "<<audible<<'\n';
    std::cout << "Network stats delivered: " << stats_received << '\n';
    return videos>=5&&audible>=5&&stats_received ? 0 : 4;
}
