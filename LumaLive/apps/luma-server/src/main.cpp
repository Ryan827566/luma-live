#include "TcpSignalingServer.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::uint16_t port = 9000;
    if (argc > 1) {
        const long p = std::strtol(argv[1], nullptr, 10);
        if (p < 1 || p > 65535) { std::cerr << "invalid port\n"; return 2; }
        port = static_cast<std::uint16_t>(p);
    }
    luma::server::signaling::TcpSignalingServer server;
    if (!server.Start(port)) { std::cerr << "failed to start LumaLive signaling server on port " << port << "\n"; return 1; }
    std::cout << "LumaLive signaling server listening on " << port << "\n";
    std::cout << "Press Enter to stop.\n";
    std::string line; std::getline(std::cin, line);
    server.Stop();
    return 0;
}
