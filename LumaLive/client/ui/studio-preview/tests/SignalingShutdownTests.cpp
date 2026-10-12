#include "TcpSignalingServer.hpp"
#include <winsock2.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
int main() {
    luma::server::signaling::TcpSignalingServer server;
    try {
        for(int round=0;round<3;++round) {
            unsigned short port=19200;
            while(port<19300&&!server.Start(port))++port;
            if(port==19300)throw std::runtime_error("No test port");
            std::vector<SOCKET> sockets;
            for(int mode=0;mode<3;++mode) {
                auto socket=::socket(AF_INET,SOCK_STREAM,0);
                sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);
                address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
                if(connect(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0)throw std::runtime_error("Connect failed");
                sockets.push_back(socket);
                if(mode==1) { const char fragment=0;send(socket,&fragment,1,0); }
                if(mode==2) { const auto length=htonl(100);send(socket,reinterpret_cast<const char*>(&length),4,0);send(socket,"x",1,0); }
            }
            std::this_thread::sleep_for(150ms);
            const auto start=std::chrono::steady_clock::now();server.Stop();
            const auto elapsed=std::chrono::steady_clock::now()-start;
            for(auto socket:sockets)closesocket(socket);
            if(elapsed>2s)throw std::runtime_error("Shutdown exceeded two seconds");
            if(server.IsRunning()||server.RoomCount()!=0)throw std::runtime_error("Server retained state");
        }
        std::cout<<"PASS: bounded stop with idle, partial header, partial body, and restart\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
