#include "TcpSignalingServer.hpp"
#include "TcpSignalingClient.hpp"
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
using T=luma::contracts::SignalingMessageType;
using M=luma::contracts::SignalingMessage;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Peer {
 luma::client::signaling::TcpSignalingClient client;std::mutex mutex;std::vector<M> inbox;std::string id;std::int64_t epoch=0;
 ~Peer(){client.Close();}
 void connect(unsigned short port,const char* name){id=name;require(client.Connect("127.0.0.1",port,[this](const M& m){std::lock_guard lock(mutex);inbox.push_back(m);}),"connect");}
 bool has(T type,const std::string& peer=""){std::lock_guard lock(mutex);for(const auto& m:inbox)if(m.type==type&&(peer.empty()||peer==m.peer_id))return true;return false;}
 M wait(T type){auto deadline=std::chrono::steady_clock::now()+2s;do{{std::lock_guard lock(mutex);for(auto it=inbox.begin();it!=inbox.end();++it)if(it->type==type){auto m=*it;inbox.erase(it);return m;}}std::this_thread::sleep_for(5ms);}while(std::chrono::steady_clock::now()<deadline);throw std::runtime_error("event timeout");}
 void send(T type,const char* target="",const char* value=""){M m;m.type=type;m.room_id="meeting-test";m.peer_id=id;m.sequence=epoch;m.target_peer_id=target;m.value=value;require(client.Send(m),"send");}
};
int main(){try{
 luma::server::signaling::TcpSignalingServer server;unsigned short port=19400;while(port<19500&&!server.Start(port))++port;require(port<19500,"server start");
 Peer host,a,b,outsider;host.connect(port,"host");a.connect(port,"a");b.connect(port,"b");outsider.connect(port,"outsider");
 a.send(T::MeetingJoin);a.wait(T::Error);
 host.send(T::MeetingCreate);auto joined=host.wait(T::MeetingJoined);host.epoch=joined.sequence;require(joined.value=="host"&&host.epoch>0,"host assignment");
 for(auto* p:{&a,&b}){p->send(T::MeetingJoin);auto ack=p->wait(T::MeetingJoined);p->epoch=ack.sequence;require(ack.value=="host"&&ack.sequence==host.epoch,"membership epoch");}
 host.wait(T::MeetingMemberJoined);host.wait(T::MeetingMemberJoined);
 a.send(T::MeetingOffer,"b");auto offer=b.wait(T::MeetingOffer);require(offer.peer_id=="a","targeted negotiation");require(!host.has(T::MeetingOffer)&&!outsider.has(T::MeetingOffer),"negotiation leaked");
 outsider.epoch=host.epoch;outsider.send(T::MeetingOffer,"b");outsider.wait(T::Error);
 a.send(T::MeetingKick,"b");a.wait(T::Error);a.send(T::MeetingEnd);a.wait(T::Error);
 a.send(T::MeetingMediaState,"","video:screen");require(b.wait(T::MeetingMediaState).value=="video:off","initial video state");
 // Drain initial peer media state before requiring the explicit screen event.
 bool screen=false;for(int i=0;i<8&&!screen;++i){auto m=b.wait(T::MeetingMediaState);screen=m.peer_id=="a"&&m.value=="video:screen";}require(screen,"media state fanout");
 host.send(T::MeetingKick,"b");b.wait(T::MeetingRemoved);a.wait(T::MeetingMemberLeft);
 b.send(T::MeetingOffer,"a");b.wait(T::Error);
 a.send(T::MeetingLeave);host.wait(T::MeetingMemberLeft);
 host.send(T::MeetingEnd);host.wait(T::MeetingEnded);
 const auto oldEpoch=host.epoch;host.send(T::MeetingCreate);host.epoch=host.wait(T::MeetingJoined).sequence;require(host.epoch!=oldEpoch,"meeting epoch reused");
 a.epoch=0;a.send(T::MeetingJoin);a.epoch=a.wait(T::MeetingJoined).sequence;
 const auto current=a.epoch;a.epoch=oldEpoch;a.send(T::MeetingOffer,"host");a.wait(T::Error);a.epoch=current;
 host.client.Close();a.wait(T::MeetingEnded);
 a.send(T::MeetingOffer,"host");a.wait(T::Error);
 server.Stop();std::cout<<"PASS: three-member creation, join, targeted signaling, media state, permissions, removal, leave, end, stale epoch and host disconnect\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
