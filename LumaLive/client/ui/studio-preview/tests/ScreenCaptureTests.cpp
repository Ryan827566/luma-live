#include "ScreenCaptureService.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
int main() {
    luma::client::media::ScreenCaptureService screen;
    std::atomic<int> frames{0},invalid{0};
    for(int run=0;run<2;++run) {
        frames=0;
        if(!screen.Start([&](auto frame){
            if(frame.width<2||frame.height<2||frame.width>1280||frame.height>720||
               (frame.width%2)||(frame.height%2)||frame.data.size()!=size_t(frame.width)*frame.height*3/2||!frame.timestamp_us)++invalid;
            ++frames;
        })) {std::cerr<<screen.LastError()<<'\n';return 1;}
        const auto end=std::chrono::steady_clock::now()+3s;
        while(frames<3&&screen.IsCapturing()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(20ms);
        screen.Stop();const int stopped=frames;
        std::this_thread::sleep_for(150ms);
        if(stopped==0&&!screen.LastError().empty()){std::cout<<"SKIP: display is unavailable in this session: "<<screen.LastError()<<'\n';return 77;}
        if(stopped<3||frames!=stopped||screen.IsCapturing()||invalid){std::cerr<<"Capture/stop failed: "<<screen.LastError()<<'\n';return 1;}
    }
    std::cout<<"PASS: actual display frames, valid I420, stop and restart\n";return 0;
}
