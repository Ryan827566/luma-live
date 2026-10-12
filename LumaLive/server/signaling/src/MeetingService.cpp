#include "MeetingService.hpp"
namespace luma::server::signaling {
using T=contracts::SignalingMessageType;
using M=contracts::SignalingMessage;
bool MeetingService::IsMeetingMessage(T type){return type>=T::MeetingCreate&&type<=T::MeetingRemoved;}
bool MeetingService::Contains(Connection c)const{std::lock_guard lock(mutex_);return bindings_.count(c)!=0;}
std::vector<MeetingService::Delivery> MeetingService::Handle(Connection c,const M& r){
    std::lock_guard lock(mutex_);std::vector<Delivery> out;
    auto error=[&](const char* why){M m;m.type=T::Error;m.room_id=r.room_id;m.target_peer_id=r.peer_id;m.sequence=r.sequence;m.value=why;out.push_back({c,m});};
    auto emit=[&](Connection destination,T type,const std::string& peer,const std::string& value,const Meeting& meeting){M m;m.type=type;m.room_id=r.room_id;m.peer_id=peer;m.value=value;m.sequence=meeting.epoch;out.push_back({destination,m});};
    if(r.type==T::MeetingCreate||r.type==T::MeetingJoin){
        if(bindings_.count(c)){error("Already in a meeting");return out;}
        if(r.room_id.empty()||r.peer_id.empty()||r.room_id.size()>128||r.peer_id.size()>128){error("Meeting and participant IDs require 1 to 128 bytes");return out;}
        auto it=meetings_.find(r.room_id);
        if(r.type==T::MeetingCreate){
            if(it!=meetings_.end()){error("Meeting already exists");return out;}
            it=meetings_.emplace(r.room_id,Meeting{r.peer_id,++nextEpoch_,{}}).first;
        }else if(it==meetings_.end()){error("Meeting does not exist");return out;}
        auto& meeting=it->second;
        if(meeting.members.size()>=6){error("Meeting capacity is six participants");return out;}
        if(meeting.members.count(r.peer_id)){error("Participant ID is already in use");return out;}
        emit(c,T::MeetingJoined,r.peer_id,meeting.host,meeting);
        for(const auto& [peer,member]:meeting.members){
            emit(c,T::MeetingMemberJoined,peer,meeting.host,meeting);
            emit(c,T::MeetingMediaState,peer,member.video,meeting);
            emit(c,T::MeetingMediaState,peer,member.audio,meeting);
            emit(member.connection,T::MeetingMemberJoined,r.peer_id,meeting.host,meeting);
        }
        meeting.members.emplace(r.peer_id,Member{c});bindings_.emplace(c,Binding{r.room_id,r.peer_id});return out;
    }
    auto binding=bindings_.find(c);
    if(binding==bindings_.end()||binding->second.meeting!=r.room_id||binding->second.member!=r.peer_id){error("Meeting membership does not match connection");return out;}
    auto& meeting=meetings_.at(r.room_id);
    if(r.sequence!=meeting.epoch){error("Stale meeting session");return out;}
    if(r.type==T::MeetingLeave)return LeaveLocked(c);
    if(r.type==T::MeetingEnd){
        if(r.peer_id!=meeting.host){error("Only the host can end a meeting");return out;}
        return LeaveLocked(c);
    }
    if(r.type==T::MeetingKick){
        if(r.peer_id!=meeting.host){error("Only the host can remove a participant");return out;}
        auto member=meeting.members.find(r.target_peer_id);
        if(member==meeting.members.end()||r.target_peer_id==meeting.host){error("Invalid removal target");return out;}
        const auto destination=member->second.connection;
        emit(destination,T::MeetingRemoved,r.target_peer_id,"Removed by host",meeting);
        auto notices=LeaveLocked(destination);out.insert(out.end(),notices.begin(),notices.end());return out;
    }
    if(r.type==T::MeetingMediaState){
        auto& member=meeting.members.at(r.peer_id);
        if(r.value=="video:off"||r.value=="video:camera"||r.value=="video:screen")member.video=r.value;
        else if(r.value=="audio:off"||r.value=="audio:on")member.audio=r.value;
        else {error("Invalid media state");return out;}
        for(const auto& [peer,other]:meeting.members)if(peer!=r.peer_id)emit(other.connection,r.type,r.peer_id,r.value,meeting);
        return out;
    }
    if(r.type==T::MeetingOffer||r.type==T::MeetingAnswer||r.type==T::MeetingIceCandidate){
        auto target=meeting.members.find(r.target_peer_id);
        if(target==meeting.members.end()||r.target_peer_id==r.peer_id){error("Target is not another meeting member");return out;}
        out.push_back({target->second.connection,r});return out;
    }
    error("Server-only or unsupported meeting message");return out;
}
std::vector<MeetingService::Delivery> MeetingService::LeaveLocked(Connection c){
    std::vector<Delivery> out;auto binding=bindings_.find(c);if(binding==bindings_.end())return out;
    const auto room=binding->second.meeting,peer=binding->second.member;auto it=meetings_.find(room);auto& meeting=it->second;
    M m;m.room_id=room;m.peer_id=peer;m.sequence=meeting.epoch;
    if(meeting.host==peer){m.type=T::MeetingEnded;m.value="Host ended or left meeting";
        for(const auto& [name,member]:meeting.members){(void)name;out.push_back({member.connection,m});bindings_.erase(member.connection);}meetings_.erase(it);
    }else{bindings_.erase(binding);meeting.members.erase(peer);m.type=T::MeetingMemberLeft;for(const auto& [name,member]:meeting.members){(void)name;out.push_back({member.connection,m});}}
    return out;
}
std::vector<MeetingService::Delivery> MeetingService::Disconnect(Connection c){std::lock_guard lock(mutex_);return LeaveLocked(c);}
}
