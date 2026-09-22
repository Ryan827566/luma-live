#include "LivePublishSession.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc,char**argv){
    if(argc<4){std::cerr<<"usage: luma_publisher <room> <peer_id> <camera_device_id> [microphone_device_id] [signaling_port]\n";return 2;}
    luma::client::live::LivePublishConfig c;c.room_id=argv[1];c.peer_id=argv[2];c.camera.device_id=argv[3];if(argc>4)c.audio.device_id=argv[4];if(argc>5)c.signaling_port=static_cast<std::uint16_t>(std::strtoul(argv[5],nullptr,10));
    c.rtc.stun_servers={"stun:stun.l.google.com:19302"};
    luma::client::live::LivePublishSession session;
    if(!session.Start(c)){std::cerr<<"failed to start: "<<session.LastError()<<"\n";return 1;}
    std::cout<<"LumaLive publisher started. Press Enter to stop.\n";std::string line;std::getline(std::cin,line);session.Stop();return 0;
}
