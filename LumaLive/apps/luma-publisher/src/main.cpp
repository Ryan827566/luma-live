#include "LivePublishSession.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: luma_publisher <room> <peer_id> <camera_device_id> "
                     "[microphone_device_id] [signaling_host] [signaling_port]\\n";
        return 2;
    }

    luma::client::live::LivePublishConfig c;
    c.room_id = argv[1];
    c.peer_id = argv[2];
    c.camera.device_id = argv[3];
    if (argc > 4) c.audio.device_id = argv[4];
    if (argc > 5) c.signaling_host = argv[5];
    if (argc > 6) {
        c.signaling_port = static_cast<std::uint16_t>(
            std::strtoul(argv[6], nullptr, 10));
    }

    c.rtc.stun_servers = {"stun:stun.l.google.com:19302"};
    if (const char* stun = std::getenv("LUMALIVE_STUN_SERVER"); stun && *stun) {
        c.rtc.stun_servers = {stun};
    }
    if (const char* turn = std::getenv("LUMALIVE_TURN_URL"); turn && *turn) {
        c.rtc.turn_url = turn;
        if (const char* user = std::getenv("LUMALIVE_TURN_USERNAME"))
            c.rtc.turn_username = user;
        if (const char* password = std::getenv("LUMALIVE_TURN_PASSWORD"))
            c.rtc.turn_password = password;
    }

    luma::client::live::LivePublishSession session;
    if (!session.Start(c)) {
        std::cerr << "failed to start: " << session.LastError() << "\\n";
        return 1;
    }

    std::cout << "LumaLive publisher started.\\n"
                 "  signaling: " << c.signaling_host << ":" << c.signaling_port << "\\n"
                 "  room: " << c.room_id << "\\n"
                 "  peer: " << c.peer_id << "\\n"
                 "Press Enter to stop.\\n";

    std::string line;
    std::getline(std::cin, line);
    session.Stop();
    return 0;
}
