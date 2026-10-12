// Local visual QA fixture: real signaling clients, all physical devices OFF.
#include "MeetingSession.hpp"
#include "TcpSignalingServer.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <array>
using Session=luma::client::ui::preview::MeetingSession;
using namespace std::chrono_literals;
int main(){
 luma::server::signaling::TcpSignalingServer server;
 if(!server.Start(19730)){std::cerr<<"Port 19730 unavailable\n";return 1;}
 std::cout<<"READY: 127.0.0.1:19730 / ui-review"<<std::endl;
 std::array<Session,2> guests;bool joined=false;
 const auto end=std::chrono::steady_clock::now()+30s;
 auto retry=std::chrono::steady_clock::now();
 while(std::chrono::steady_clock::now()<end){
  if(std::chrono::steady_clock::now()>=retry){
   for(int i=0;i<2;++i)if(guests[i].GetState()==Session::State::Offline||guests[i].GetState()==Session::State::Failed)
    guests[i].Start("127.0.0.1",19730,"ui-review",i==0?"review-alex":"review-sam",false);
   retry=std::chrono::steady_clock::now()+500ms;
  }
  for(auto& guest:guests)guest.Poll();
  if(guests[0].GetState()==Session::State::Joined&&guests[1].GetState()==Session::State::Joined){if(!joined)std::cout<<"JOINED: two real participants, cameras and microphones off"<<std::endl;joined=true;}
  if(joined&&guests[0].GetState()==Session::State::Ended)break;
  std::this_thread::sleep_for(10ms);
 }
 for(auto& guest:guests)guest.Leave();server.Stop();return joined?0:1;
}
