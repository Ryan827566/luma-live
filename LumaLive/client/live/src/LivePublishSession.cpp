#include "LivePublishSession.hpp"
#include <mutex>
#include <utility>

namespace luma::client::live {
LivePublishSession::LivePublishSession() = default;
LivePublishSession::~LivePublishSession(){Stop();}
std::string LivePublishSession::LastError() const { std::lock_guard lock(mutex_); return last_error_; }
bool LivePublishSession::Start(const LivePublishConfig& cfg){
    std::lock_guard lock(mutex_);
    if(running_) return false;
    if(cfg.room_id.empty()||cfg.peer_id.empty()){ last_error_="room_id and peer_id are required"; return false; }

    room_id_=cfg.room_id; peer_id_=cfg.peer_id; remote_peer_id_.clear();
    capture_=luma::client::media::CreateDeviceCaptureService();
    pipeline_=luma::client::media::pipeline::CreateMediaPipelineService();
    rtc_=std::make_shared<luma::client::webrtc::NativeWebRtcPeerConnection>();
    signaling_=std::make_unique<luma::client::signaling::TcpSignalingClient>();

    luma::client::webrtc::WebRtcCallbacks cb;
    cb.on_local_description=[this](const std::string&type,const std::string&sdp){
        if(!signaling_||!signaling_->IsConnected())return;
        luma::contracts::SignalingMessage m;m.type=type=="offer"?luma::contracts::SignalingMessageType::Offer:luma::contracts::SignalingMessageType::Answer;
        m.room_id=room_id_;m.peer_id=peer_id_;m.target_peer_id=remote_peer_id_;m.sdp=sdp;signaling_->Send(m);
    };
    cb.on_local_ice_candidate=[this](const std::string&mid,int line,const std::string&candidate){
        if(!signaling_||!signaling_->IsConnected())return;
        luma::contracts::SignalingMessage m;m.type=luma::contracts::SignalingMessageType::IceCandidate;m.room_id=room_id_;m.peer_id=peer_id_;m.target_peer_id=remote_peer_id_;
        m.value=std::to_string(line);m.candidate=candidate;m.candidate_mid=mid;signaling_->Send(m);
    };

    if(!rtc_->Initialize(cfg.rtc,std::move(cb))){last_error_="WebRTC initialization failed";Cleanup();return false;}
    pipeline_->SetFrameSink(rtc_);
    if(!pipeline_->Start().success){last_error_="pipeline start failed";Cleanup();return false;}
    if(!capture_->Start().IsOk()){last_error_="capture start failed";Cleanup();return false;}
    if(!capture_->StartCamera(cfg.camera,[this](auto f){pipeline_->PushVideoFrame(std::move(f));}).IsOk()){last_error_="camera start failed";Cleanup();return false;}
    if(!capture_->StartMicrophone(cfg.audio,[this](auto f){pipeline_->PushAudioFrame(std::move(f));}).IsOk()){last_error_="microphone start failed";Cleanup();return false;}

    if(!signaling_->Connect(cfg.signaling_host,cfg.signaling_port,[this](const auto&m){HandleSignal(m);})){last_error_="signaling connection failed";Cleanup();return false;}
    luma::contracts::SignalingMessage join;join.type=luma::contracts::SignalingMessageType::JoinRoom;join.room_id=room_id_;join.peer_id=peer_id_;
    if(!signaling_->Send(join)){last_error_="join room failed";Cleanup();return false;}

    running_=true;
    return true;
}
void LivePublishSession::HandleSignal(const luma::contracts::SignalingMessage&m){
    if(m.room_id!=room_id_)return;
    switch(m.type){
    case luma::contracts::SignalingMessageType::PeerJoined:
        if(m.peer_id==peer_id_)return; remote_peer_id_=m.peer_id; if(peer_id_<m.peer_id)rtc_->CreateOffer(); break;
    case luma::contracts::SignalingMessageType::Offer:
        if(m.peer_id==peer_id_)return; remote_peer_id_=m.peer_id; if(rtc_->SetRemoteDescription("offer",m.sdp))rtc_->CreateAnswer(); break;
    case luma::contracts::SignalingMessageType::Answer:
        if(m.peer_id==peer_id_)return; remote_peer_id_=m.peer_id; rtc_->SetRemoteDescription("answer",m.sdp); break;
    case luma::contracts::SignalingMessageType::IceCandidate:{int line=0;try{line=std::stoi(m.value);}catch(...){return;}rtc_->AddRemoteIceCandidate(m.candidate_mid,line,m.candidate);break;}
    default: break; }
}
void LivePublishSession::Cleanup(){
    if(signaling_&&running_){
        luma::contracts::SignalingMessage m;m.type=luma::contracts::SignalingMessageType::LeaveRoom;m.room_id=room_id_;m.peer_id=peer_id_;signaling_->Send(m);
    }
    if(capture_){capture_->StopCamera();capture_->StopMicrophone();capture_->Stop();}
    if(pipeline_){pipeline_->Stop();pipeline_->SetFrameSink(nullptr);}
    if(rtc_)rtc_->Close();
    if(signaling_)signaling_->Close();
    running_=false;
    signaling_.reset();rtc_.reset();pipeline_.reset();capture_.reset();
}
void LivePublishSession::Stop(){std::lock_guard lock(mutex_); Cleanup();}
}
