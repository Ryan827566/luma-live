#pragma once
#include "TcpSignalingClient.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
namespace luma::client::ui::preview {
// Public operations run on one UI/control thread. Transport callbacks only queue.
class MeetingSession {
public:
    enum class State { Offline, Joining, Joined, Ended, Removed, Failed };
    struct Member { std::string id; bool host{false}; std::string video{"off"}; bool microphone{false}; };
    using Message=contracts::SignalingMessage;
    using Type=contracts::SignalingMessageType;
    ~MeetingSession(){Leave();}
    State GetState()const{return state_;}
    const std::map<std::string,Member>& Members()const{return members_;}
    const std::string& Host()const{return host_;}
    const std::string& Status()const{return status_;}
    std::int64_t Epoch()const{return epoch_;}
    bool IsHost()const{return state_==State::Joined&&host_==self_;}
    bool Start(const std::string& server,std::uint16_t port,const std::string& meeting,const std::string& participant,bool create){
        Leave();meeting_=meeting;self_=participant;
        if(server.empty()||!port||meeting.empty()||meeting.size()>128||participant.empty()||participant.size()>128){Finish(State::Failed,"Invalid meeting or participant");return false;}
        overflow_=false;
        if(!transport_.Connect(server,port,[this](const Message& message){std::lock_guard lock(mutex_);if(events_.size()<512)events_.push_back(message);else overflow_=true;})){Finish(State::Failed,"Signaling connection failed");return false;}
        state_=State::Joining;deadline_=Clock::now()+std::chrono::seconds(5);status_="Joining meeting";
        if(!Send(create?Type::MeetingCreate:Type::MeetingJoin)){Finish(State::Failed,"Registration send failed");return false;}return true;
    }
    void Leave(){if(state_==State::Joined)Send(Type::MeetingLeave);Finish(State::Offline,"Left meeting");}
    bool End(){return IsHost()&&Send(Type::MeetingEnd);}
    bool Remove(const std::string& member){return IsHost()&&member!=self_&&members_.count(member)&&Send(Type::MeetingKick,member);}
    bool SetVideo(const std::string& source){if(state_!=State::Joined||(source!="off"&&source!="camera"&&source!="screen"))return false;if(!Send(Type::MeetingMediaState,"","video:"+source))return false;members_.at(self_).video=source;return true;}
    bool SetMicrophone(bool enabled){if(state_!=State::Joined||!Send(Type::MeetingMediaState,"",enabled?"audio:on":"audio:off"))return false;members_.at(self_).microphone=enabled;return true;}
    // The media controller owns SDP/ICE negotiation; never forward call messages.
    bool SendMedia(Message message){if(state_!=State::Joined||message.target_peer_id==self_||!members_.count(message.target_peer_id)||!IsMedia(message.type))return false;message.room_id=meeting_;message.peer_id=self_;message.sequence=epoch_;return transport_.Send(message);}
    void Poll(const std::function<void(const Message&)>& onMedia={}){
        std::deque<Message> batch;{std::lock_guard lock(mutex_);batch.swap(events_);}
        if(overflow_){Finish(State::Failed,"Meeting event queue overflow");return;}
        for(const auto& m:batch){
            if(m.room_id!=meeting_)continue;
            if(state_==State::Joining){
                if(m.type==Type::Error&&m.target_peer_id==self_){Finish(State::Failed,m.value);return;}
                if(m.type==Type::MeetingJoined&&m.peer_id==self_&&m.sequence>0&&!m.value.empty()){
                    epoch_=m.sequence;host_=m.value;members_[self_]=Member{self_,self_==host_};state_=State::Joined;status_="Joined meeting";
                }
                continue;
            }
            if(state_!=State::Joined||m.sequence!=epoch_)continue;
            if(m.type==Type::MeetingEnded){Finish(State::Ended,m.value);return;}
            if(m.type==Type::MeetingRemoved&&m.peer_id==self_){Finish(State::Removed,m.value);return;}
            if(m.type==Type::MeetingMemberJoined&&!m.peer_id.empty()){members_.try_emplace(m.peer_id,Member{m.peer_id,m.peer_id==host_});continue;}
            if(m.type==Type::MeetingMemberLeft){members_.erase(m.peer_id);continue;}
            if(m.type==Type::Error&&m.target_peer_id==self_){status_=m.value;continue;}
            auto member=members_.find(m.peer_id);if(member==members_.end())continue;
            if(m.type==Type::MeetingMediaState){if(m.value=="audio:on"||m.value=="audio:off")member->second.microphone=m.value=="audio:on";else if(m.value=="video:off"||m.value=="video:camera"||m.value=="video:screen")member->second.video=m.value.substr(6);}
            else if(IsMedia(m.type)&&m.target_peer_id==self_&&m.peer_id!=self_&&onMedia)onMedia(m);
        }
        if((state_==State::Joined||state_==State::Joining)&&!transport_.IsConnected())Finish(State::Failed,"Signaling disconnected");
        else if(state_==State::Joining&&Clock::now()>=deadline_)Finish(State::Failed,"Meeting registration timed out");
    }
private:
    using Clock=std::chrono::steady_clock;
    static bool IsMedia(Type t){return t==Type::MeetingOffer||t==Type::MeetingAnswer||t==Type::MeetingIceCandidate;}
    bool Send(Type type,const std::string& target="",const std::string& value=""){Message m;m.type=type;m.room_id=meeting_;m.peer_id=self_;m.sequence=epoch_;m.target_peer_id=target;m.value=value;return transport_.Send(m);}
    void Finish(State state,const std::string& status){transport_.Close();{std::lock_guard lock(mutex_);events_.clear();}members_.clear();host_.clear();epoch_=0;state_=state;status_=status;}
    signaling::TcpSignalingClient transport_;
    std::mutex mutex_;std::deque<Message> events_;std::atomic<bool> overflow_{false};
    std::string meeting_,self_,host_,status_;std::int64_t epoch_{0};State state_{State::Offline};Clock::time_point deadline_{};
    std::map<std::string,Member> members_;
};
}
