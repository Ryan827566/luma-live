#pragma once
#include "MeetingSession.hpp"
#include "MeetingAudioMixer.hpp"
#include "NativeWebRtcPeerConnection.hpp"
#include <memory>
#include <vector>
namespace luma::client::ui::preview {
// Small-meeting mesh: one independently owned WebRTC transport per remote member.
// Poll/control are UI-thread operations; local frame input may be capture-thread.
class MeetingMedia {
public:
 using Video=media::pipeline::VideoFrame;using Audio=media::pipeline::AudioFrame;
 using Message=MeetingSession::Message;using Type=MeetingSession::Type;
 struct Callbacks{std::function<void(const std::string&,Video)> video;std::function<void(const std::string&,Audio)> audio;};
 ~MeetingMedia(){Leave();}
 MeetingSession& Session(){return session_;}
 const std::string& Error()const{return error_;}
 Audio MixAudio(){return mixer_.Pull();}
 bool Start(const std::string& server,std::uint16_t port,const std::string& meeting,const std::string& self,bool create,Callbacks cb,const contracts::PeerConnectionConfig& config={}){
  Leave();self_=self;callbacks_=std::move(cb);config_=config;error_.clear();return session_.Start(server,port,meeting,self,create);
 }
 bool SetVideo(const std::string& source){if(!session_.SetVideo(source))return false;videoEnabled_=source!="off";return true;}
 bool SetMicrophone(bool enabled){if(!session_.SetMicrophone(enabled))return false;audioEnabled_=enabled;return true;}
 void VideoFrame(const Video& frame){std::lock_guard lock(linksMutex_);if(videoEnabled_)for(auto& [id,link]:links_)if(link->active)link->rtc->AddVideoFrame(frame);}
 void AudioFrame(const Audio& frame){std::lock_guard lock(linksMutex_);if(audioEnabled_)for(auto& [id,link]:links_)if(link->active)link->rtc->AddAudioFrame(frame);}
 void Leave(){videoEnabled_=false;audioEnabled_=false;session_.Leave();Clear();}
 void Poll(){
  std::vector<Message> incoming;session_.Poll([&](const Message& m){incoming.push_back(m);});
  if(session_.GetState()!=MeetingSession::State::Joined){videoEnabled_=false;audioEnabled_=false;Clear();return;}
  {
   std::lock_guard lock(linksMutex_);
   for(auto it=links_.begin();it!=links_.end();){auto member=session_.Members().find(it->first);if(member==session_.Members().end()||member->second.instance!=it->second->instance){Close(*it->second);mixer_.Remove(it->first);it=links_.erase(it);}else ++it;}
   for(const auto& [id,member]:session_.Members())if(id!=self_){
    if(!links_.count(id)){auto link=std::make_shared<Link>();link->instance=member.instance;link->generation=++generation_;link->initiator=self_<id;link->nonce=link->initiator?std::to_string(++nextNonce_):"";link->rtc=webrtc::NativeWebRtcPeerConnection::Create();links_[id]=link;
     auto cb=Bind(id,link);if(!link->rtc->Initialize(config_,std::move(cb))){Fail(*link,"Media initialization failed for "+id);continue;}
     if(link->initiator&&!link->rtc->CreateOffer())Fail(*link,"Offer failed for "+id);
    }
    links_[id]->video=member.video!="off";links_[id]->audio=member.microphone;if(!member.microphone)mixer_.Remove(id);
   }
   for(const auto& m:incoming){auto it=links_.find(m.peer_id);if(it==links_.end()||!it->second->active)continue;auto& link=*it->second;
    if(m.type==Type::MeetingOffer&&!link.initiator&&link.nonce.empty()&&!m.value.empty()){link.nonce=m.value;if(!link.rtc->SetRemoteDescription("offer",m.sdp))Fail(link,"Invalid meeting offer");}
    else if(m.type==Type::MeetingAnswer&&link.initiator&&!link.remoteSet&&m.value==link.nonce){if(!link.rtc->SetRemoteDescription("answer",m.sdp))Fail(link,"Invalid meeting answer");}
    else if(m.type==Type::MeetingIceCandidate){if(!link.nonce.empty()&&m.sdp!=link.nonce)continue;if(link.remoteSet){if(!AddIce(link,m))Fail(link,"Invalid meeting ICE");}else if(link.ice.size()<128)link.ice.push_back(m);else Fail(link,"Meeting ICE queue overflow");}
   }
   std::deque<Event> events;{std::lock_guard lock(eventsMutex_);events.swap(events_);}
   if(overflow_){for(auto& [id,link]:links_)Fail(*link,"Meeting media event overflow");}
   for(auto& event:events){auto it=links_.find(event.peer);if(it==links_.end()||it->second->generation!=event.generation||!it->second->active)continue;auto& link=*it->second;
    if(event.kind==0){event.message.target_peer_id=event.peer;if(event.message.type==Type::MeetingIceCandidate)event.message.sdp=link.nonce;else event.message.value=link.nonce;if(!session_.SendMedia(event.message))Fail(link,"Meeting signaling send failed");}
    else if(event.kind==1){link.remoteSet=true;for(const auto& m:link.ice)if(m.sdp==link.nonce&&!AddIce(link,m))Fail(link,"Invalid queued ICE");link.ice.clear();if(!link.initiator&&link.active&&!link.rtc->CreateAnswer())Fail(link,"Answer failed");}
    else if(event.message.value=="failed"||event.message.value.find("error")!=std::string::npos)Fail(link,"Peer "+event.peer+": "+event.message.value);
   }
  }
 }
private:
 struct Link{std::shared_ptr<webrtc::NativeWebRtcPeerConnection> rtc;std::uint64_t generation{},instance{};std::atomic<bool> active{true},video{false},audio{false};bool initiator=false,remoteSet=false;std::string nonce;std::vector<Message> ice;};
 struct Event{std::string peer;std::uint64_t generation;int kind;Message message;};
 void Queue(Event e){std::lock_guard lock(eventsMutex_);if(events_.size()<512)events_.push_back(std::move(e));else overflow_=true;}
 webrtc::WebRtcCallbacks Bind(const std::string& id,const std::shared_ptr<Link>& link){
  webrtc::WebRtcCallbacks cb;std::weak_ptr<Link> weak=link;const auto generation=link->generation;
  cb.on_local_description=[this,id,generation](const std::string& type,const std::string& sdp){Message m;m.type=type=="offer"?Type::MeetingOffer:Type::MeetingAnswer;m.sdp=sdp;Queue({id,generation,0,m});};
  cb.on_local_ice_candidate=[this,id,generation](const std::string& mid,int line,const std::string& candidate){Message m;m.type=Type::MeetingIceCandidate;m.candidate_mid=mid;m.value=std::to_string(line);m.candidate=candidate;Queue({id,generation,0,m});};
  cb.on_remote_description_set=[this,id,generation]{Queue({id,generation,1,{}});};
  cb.on_connection_state=[this,id,generation](const std::string& state){Message m;m.value=state;Queue({id,generation,2,m});};
  auto video=callbacks_.video;auto audio=callbacks_.audio;
  cb.on_remote_video=[id,weak,video](Video frame){if(auto p=weak.lock();p&&p->active&&p->video&&video)video(id,std::move(frame));};
  cb.on_remote_audio=[this,id,weak,audio](Audio frame){if(auto p=weak.lock();p&&p->active&&p->audio){mixer_.Push(id,frame);if(audio)audio(id,std::move(frame));}};return cb;
 }
 bool AddIce(Link& link,const Message& m){try{size_t used=0;int line=std::stoi(m.value,&used);return used==m.value.size()&&line>=0&&link.rtc->AddRemoteIceCandidate(m.candidate_mid,line,m.candidate);}catch(...){return false;}}
 void Close(Link& link){link.active=false;if(link.rtc)link.rtc->Close();}
 void Fail(Link& link,const std::string& reason){error_=reason;Close(link);}
 void Clear(){std::lock_guard lock(linksMutex_);for(auto& [id,link]:links_)Close(*link);links_.clear();mixer_.Clear();{std::lock_guard lock(eventsMutex_);events_.clear();}overflow_=false;}
 MeetingAudioMixer mixer_;MeetingSession session_;std::string self_,error_;Callbacks callbacks_;contracts::PeerConnectionConfig config_;
 std::mutex linksMutex_,eventsMutex_;std::map<std::string,std::shared_ptr<Link>> links_;std::deque<Event> events_;std::atomic<bool> videoEnabled_{false},audioEnabled_{false},overflow_{false};std::uint64_t generation_{0};
 inline static std::atomic<std::uint64_t> nextNonce_{0};
};
}
