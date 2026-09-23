#pragma once
#include "NativeWebRtcPeerConnection.hpp"
#include "TcpSignalingClient.hpp"
#include <deque>
#include <mutex>
#include <atomic>
#include <cstdlib>

namespace luma::client::ui::preview {
// All negotiation runs on the UI/controller thread through Poll(). Worker
// callbacks enqueue values, never touch HWNDs or call teardown recursively.
class PreviewCall {
    struct Event {int kind{};contracts::SignalingMessage signal;std::string type,text;};
    std::mutex mutex_;
    std::mutex mediaMutex_;
    std::deque<Event> events_;
    std::shared_ptr<webrtc::NativeWebRtcPeerConnection> rtc_;
    signaling::TcpSignalingClient signaling_;
    std::string room_,peer_,remote_;
    bool remoteSet_{false},answerPending_{false};
    std::vector<contracts::SignalingMessage> pendingIce_;
    std::atomic<bool> accepting_{false};
    std::atomic<bool> mediaReady_{false};
    void Queue(Event e){std::lock_guard lock(mutex_);if(accepting_&&events_.size()<256)events_.push_back(std::move(e));}
    void Send(contracts::SignalingMessage m){m.room_id=room_;m.peer_id=peer_;m.target_peer_id=remote_;signaling_.Send(m);}
public:
    ~PreviewCall(){Stop();}
    bool Active()const{return accepting_;}
    bool Start(const std::string& host,uint16_t port,const std::string& room,const std::string& peer,webrtc::WebRtcCallbacks cb){
        Stop();room_=room;peer_=peer;accepting_=true;
        {std::lock_guard lock(mediaMutex_);rtc_=webrtc::NativeWebRtcPeerConnection::Create();}
        cb.on_local_description=[this](const auto& type,const auto& sdp){Queue({1,{},type,sdp});};
        cb.on_local_ice_candidate=[this](const auto& mid,int line,const auto& candidate){contracts::SignalingMessage m;m.type=contracts::SignalingMessageType::IceCandidate;m.candidate_mid=mid;m.value=std::to_string(line);m.candidate=candidate;Queue({2,std::move(m),{}, {}});};
        cb.on_remote_description_set=[this]{Queue({3,{},{},{}});};
        cb.on_connection_state=[this](const auto& state){Queue({4,{},{},state});};
        contracts::PeerConnectionConfig config;
        // Use STUN by default so two clients can establish direct P2P media
        // across ordinary NATs. Production deployments can override this and
        // provide TURN through environment variables.
        const char* stun = std::getenv("LUMALIVE_STUN_SERVER");
        config.stun_servers.push_back(stun && *stun ? stun : "stun:stun.l.google.com:19302");
        const char* turn = std::getenv("LUMALIVE_TURN_URL");
        const char* turnUser = std::getenv("LUMALIVE_TURN_USERNAME");
        const char* turnPassword = std::getenv("LUMALIVE_TURN_PASSWORD");
        if (turn && *turn) {
            config.turn_url = turn;
            config.turn_username = turnUser ? turnUser : "";
            config.turn_password = turnPassword ? turnPassword : "";
        }
        if(!rtc_->Initialize(config,std::move(cb))){Stop();return false;}
        mediaReady_=true;
        if(!signaling_.Connect(host,port,[this](const auto& m){Queue({0,m,{},{}});})){Stop();return false;}
        contracts::SignalingMessage join;join.type=contracts::SignalingMessageType::JoinRoom;Send(join);return true;
    }
    void Video(const media::pipeline::VideoFrame& f){std::lock_guard lock(mediaMutex_);if(rtc_&&mediaReady_)rtc_->AddVideoFrame(f);}
    void Audio(const media::pipeline::AudioFrame& f){std::lock_guard lock(mediaMutex_);if(rtc_&&mediaReady_)rtc_->AddAudioFrame(f);}
    std::string Poll(){
        std::deque<Event> work;{std::lock_guard lock(mutex_);work.swap(events_);}std::string status;
        using T=contracts::SignalingMessageType;
        for(auto& e:work){
            if(e.kind==4){status=e.text;continue;}
            if(e.kind==1){contracts::SignalingMessage m;m.type=e.type=="offer"?T::Offer:T::Answer;m.sdp=e.text;Send(m);continue;}
            if(e.kind==2){Send(e.signal);continue;}
            if(e.kind==3){remoteSet_=true;for(auto& m:pendingIce_){try{rtc_->AddRemoteIceCandidate(m.candidate_mid,std::stoi(m.value),m.candidate);}catch(...){}}pendingIce_.clear();if(answerPending_){answerPending_=false;rtc_->CreateAnswer();}continue;}
            auto& m=e.signal;if(m.room_id!=room_||m.peer_id==peer_)continue;
            if(!remote_.empty()&&m.peer_id!=remote_)continue; // explicit 1:1 session
            switch(m.type){
            case T::PeerJoined:remote_=m.peer_id;if(peer_<remote_)rtc_->CreateOffer();status="waiting for peer";break;
            case T::Offer:remote_=m.peer_id;remoteSet_=false;answerPending_=true;if(!rtc_->SetRemoteDescription("offer",m.sdp))status="invalid remote offer";break;
            case T::Answer:remote_=m.peer_id;remoteSet_=false;if(!rtc_->SetRemoteDescription("answer",m.sdp))status="invalid remote answer";break;
            case T::IceCandidate:if(!remoteSet_){if(pendingIce_.size()<128)pendingIce_.push_back(m);}else{try{rtc_->AddRemoteIceCandidate(m.candidate_mid,std::stoi(m.value),m.candidate);}catch(...){}}break;
            case T::PeerLeft:status="peer left; leave and rejoin to reconnect";break;
            default:break;
            }
        }
        if(Active()&&!signaling_.IsConnected())status="signaling disconnected";
        return status;
    }
    void Stop(){accepting_=false;mediaReady_=false;signaling_.Close();{std::lock_guard lock(mediaMutex_);if(rtc_)rtc_->Close();rtc_.reset();}std::lock_guard lock(mutex_);events_.clear();pendingIce_.clear();remote_.clear();remoteSet_=false;answerPending_=false;}
};
}

