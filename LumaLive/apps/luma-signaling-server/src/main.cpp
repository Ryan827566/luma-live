#include "TcpSignalingServer.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
std::uint16_t ParsePort(int argc, char** argv) {
    if (argc < 2) return 9000;
    char* end = nullptr;
    const auto value = std::strtoul(argv[1], &end, 10);
    if (!end || *end != '\0' || value < 1 || value > 65535) return 0;
    return static_cast<std::uint16_t>(value);
}
}

int main(int argc, char** argv) {
    const auto port = ParsePort(argc, argv);
    if (port == 0) {
        std::cerr << "usage: luma_signaling_server [port]\n";
        return 2;
    }

    luma::server::signaling::TcpSignalingServer server;
    if (!server.Start(port)) {
        std::cerr << "failed to start signaling server on TCP " << port << "\n";
        return 1;
    }

    std::cout << "LumaLive signaling server started on TCP " << port << "\n";
    std::cout << "This server carries room/SDP/ICE signaling; WebRTC media remains peer-to-peer.\n";
    std::cout << "Press Enter to stop.\n";

    std::string line;
    std::getline(std::cin, line);
    server.Stop();
    return 0;
}
