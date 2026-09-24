#include "MeetingSession.hpp"
#include "TcpSignalingServer.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using Session=luma::client::ui::preview::MeetingSession;
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void until(Session& a,Session& b,Session& c,F ready){const auto end=std::chrono::steady_clock::now()+3s;do{a.Poll();b.Poll();c.Poll();if(ready())return;std::this_thread::sleep_for(5ms);}while(std::chrono::steady_clock::now()<end);throw std::runtime_error("client state timeout");}
int main(){try{
 luma::server::signaling::TcpSignalingServer server;unsigned short port=19500;while(port<19600&&!server.Start(port))++port;require(port<19600,"server start");
 Session host,a,b;
 require(host.Start("127.0.0.1",port,"client-test","host",true),"create");until(host,a,b,[&]{return host.IsHost();});
 require(a.Start("127.0.0.1",port,"client-test","a",false),"join a");require(b.Start("127.0.0.1",port,"client-test","b",false),"join b");
 until(host,a,b,[&]{return host.Members().size()==3&&a.Members().size()==3&&b.Members().size()==3;});
 require(a.Host()=="host"&&!a.IsHost()&&!a.End()&&!a.Remove("b"),"non-host permissions");
 require(a.SetVideo("screen")&&a.SetMicrophone(true),"media state update");until(host,a,b,[&]{return b.Members().at("a").video=="screen"&&b.Members().at("a").microphone;});
 require(host.Remove("b"),"host remove");until(host,a,b,[&]{return b.GetState()==Session::State::Removed&&host.Members().size()==2&&a.Members().size()==2;});require(b.Members().empty()&&!b.SetVideo("camera"),"removed member retained controls");
 require(b.Start("127.0.0.1",port,"client-test","b",false),"rejoin");until(host,a,b,[&]{return b.Members().size()==3&&host.Members().size()==3;});
 a.Leave();until(host,a,b,[&]{return host.Members().size()==2&&b.Members().size()==2;});
 require(host.End(),"host end");until(host,a,b,[&]{return host.GetState()==Session::State::Ended&&b.GetState()==Session::State::Ended;});require(host.Members().empty()&&b.Members().empty(),"end retained roster");
 require(a.Start("127.0.0.1",port,"missing","a",false),"missing connect");until(host,a,b,[&]{return a.GetState()==Session::State::Failed;});
 server.Stop();std::cout<<"PASS: meeting client roster, roles, media state, removal, rejoin, leave, end and failure\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
