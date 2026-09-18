#include "NativeWebRtcPeerConnection.hpp"

#if defined(LUMALIVE_HAS_WEBRTC)
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media_with_defaults.h"
#include "api/environment/environment_factory.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_broadcaster.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/thread.h"
#include <condition_variable>
#include <mutex>
#include <set>

namespace luma::client::webrtc {
namespace {

class LocalVideoSource final : public ::web::rtc::Notifier<::web::rtc::VideoTrackSourceInterface> {
public:
    static ::rtc::scoped_refptr<LocalVideoSource> Create() { return ::rtc::make_ref_counted<LocalVideoSource>(); }
    ::web::rtc::MediaSourceInterface::SourceState state() const override { return state_; }
    bool remote() const override { return false; }
    void RegisterObserver(::web::rtc::ObserverInterface* o) override { observers_.insert(o); }
    void UnregisterObserver(::web::rtc::ObserverInterface* o) override { observers_.erase(o); }
    void AddOrUpdateSink(::rtc::VideoSinkInterface<::web::rtc::VideoFrame>* sink, const ::rtc::VideoSinkWants& wants) override { broadcaster_.AddOrUpdateSink(sink, wants); }
    void RemoveSink(::rtc::VideoSinkInterface<::web::rtc::VideoFrame>* sink) override { broadcaster_.RemoveSink(sink); }
    bool is_screencast() const override { return false; }
    std::optional<bool> needs_denoising() const override { return std::nullopt; }
    bool GetStats(::web::rtc::VideoTrackSourceInterface::Stats* stats) override { if (!stats || width_ == 0) return false; stats->input_width=width_; stats->input_height=height_; return true; }
    void Push(const ::web::rtc::VideoFrame& frame) { width_=frame.width(); height_=frame.height(); broadcaster_.OnFrame(frame); }
private:
    ::rtc::VideoBroadcaster broadcaster_;
    ::web::rtc::MediaSourceInterface::SourceState state_{::web::rtc::MediaSourceInterface::kLive};
    std::set<::web::rtc::ObserverInterface*> observers_;
    int width_{0}, height_{0};
};

class LocalAudioSource final : public ::web::rtc::Notifier<::web::rtc::AudioSourceInterface> {
public:
    ::web::rtc::MediaSourceInterface::SourceState state() const override { return ::web::rtc::MediaSourceInterface::kLive; }
    bool remote() const override { return false; }
    void RegisterObserver(::web::rtc::ObserverInterface*) override {}
    void UnregisterObserver(::web::rtc::ObserverInterface*) override {}
    void AddSink(::web::rtc::AudioTrackSinkInterface* sink) override { std::lock_guard lock(m_); sinks_.insert(sink); }
    void RemoveSink(::web::rtc::AudioTrackSinkInterface* sink) override { std::lock_guard lock(m_); sinks_.erase(sink); }
    void Push(const void* data,int bits,int rate,size_t channels,size_t frames){std::lock_guard lock(m_);for(auto*s:sinks_)s->OnData(data,bits,rate,channels,frames);}
private:
    std::mutex m_;
    std::set<::web::rtc::AudioTrackSinkInterface*> sinks_;
};

class DescriptionObserver final : public ::web::rtc::CreateSessionDescriptionObserver {
public:
    explicit DescriptionObserver(std::function<void(::web::rtc::SessionDescriptionInterface*)> ok,std::function<void(const std::string&)> fail):ok_(std::move(ok)),fail_(std::move(fail)){}
    void OnSuccess(::web::rtc::SessionDescriptionInterface* d) override { ok_(d); }
    void OnFailure(::web::rtc::RTCError e) override { fail_(e.message()); }
protected: ~DescriptionObserver() override = default;
private: std::function<void(::web::rtc::SessionDescriptionInterface*)> ok_; std::function<void(const std::string&)> fail_;
};
class SetObserver final : public ::web::rtc::SetSessionDescriptionObserver {
public:
    explicit SetObserver(std::function<void()> ok,std::function<void(const std::string&)> fail):ok_(std::move(ok)),fail_(std::move(fail)){}
    void OnSuccess() override {ok_();}
    void OnFailure(::web::rtc::RTCError e) override {fail_(e.message());}
protected: ~SetObserver() override = default;
private: std::function<void()> ok_; std::function<void(const std::string&)> fail_;
};

class PcObserver final : public ::web::rtc::PeerConnectionObserver {
public:
    explicit PcObserver(WebRtcCallbacks& cb):cb_(cb){}
    void OnSignalingChange(::web::rtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(::rtc::scoped_refptr<::web::rtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(::web::rtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnConnectionChange(::web::rtc::PeerConnectionInterface::PeerConnectionState state) override { if(cb_.on_connection_state) cb_.on_connection_state(std::string(::web::rtc::PeerConnectionInterface::AsString(state))); }
    void OnIceCandidate(const ::web::rtc::IceCandidate* c) override { if(cb_.on_local_ice_candidate){std::string s; c->ToString(&s); cb_.on_local_ice_candidate(c->sdp_mid(), c->sdp_mline_index(), s);} }
    void OnTrack(::rtc::scoped_refptr<::web::rtc::RtpTransceiverInterface> transceiver) override {
        if(!transceiver || !transceiver->receiver()) return; auto track=transceiver->receiver()->track(); if(!track) return;
        if(track->kind()==::web::rtc::MediaStreamTrackInterface::kVideoKind){ auto video=static_cast<::web::rtc::VideoTrackInterface*>(track.get()); video->AddOrUpdateSink(&video_sink_,::rtc::VideoSinkWants()); }
        else if(track->kind()==::web::rtc::MediaStreamTrackInterface::kAudioKind){ auto audio=static_cast<::web::rtc::AudioTrackInterface*>(track.get()); audio->AddSink(&audio_sink_); }
    }
private:
    class VideoSink final : public ::rtc::VideoSinkInterface<::web::rtc::VideoFrame> { public: explicit VideoSink(WebRtcCallbacks& cb):cb_(cb){} void OnFrame(const ::web::rtc::VideoFrame&) override { if(cb_.on_remote_video_frame) cb_.on_remote_video_frame(); } private: WebRtcCallbacks& cb_; };
    class AudioSink final : public ::web::rtc::AudioTrackSinkInterface { public: explicit AudioSink(WebRtcCallbacks& cb):cb_(cb){} void OnData(const void*,int,int,size_t,size_t) override { if(cb_.on_remote_audio_frame) cb_.on_remote_audio_frame(); } private: WebRtcCallbacks& cb_; };
    WebRtcCallbacks& cb_; VideoSink video_sink_{cb_}; AudioSink audio_sink_{cb_};
};
}

struct NativeWebRtcPeerConnection::Impl {
    WebRtcCallbacks cb;
    ::rtc::scoped_refptr<::web::rtc::PeerConnectionFactoryInterface> factory;
    ::rtc::scoped_refptr<::web::rtc::PeerConnectionInterface> pc;
    ::rtc::scoped_refptr<LocalVideoSource> video_source;
    ::rtc::scoped_refptr<LocalAudioSource> audio_source;
    std::unique_ptr<PcObserver> observer;
};

NativeWebRtcPeerConnection::NativeWebRtcPeerConnection():impl_(std::make_unique<Impl>()){}
NativeWebRtcPeerConnection::~NativeWebRtcPeerConnection(){Close();}
bool NativeWebRtcPeerConnection::Initialize(const luma::contracts::PeerConnectionConfig& config, WebRtcCallbacks callbacks){
    if(impl_->pc) return false; impl_->cb=std::move(callbacks); impl_->observer=std::make_unique<PcObserver>(impl_->cb);
    ::web::rtc::PeerConnectionFactoryDependencies deps; deps.signaling_thread=::rtc::Thread::Current(); deps.env=::web::rtc::CreateEnvironment(); ::web::rtc::EnableMediaWithDefaults(deps); impl_->factory=::web::rtc::CreateModularPeerConnectionFactory(std::move(deps)); if(!impl_->factory)return false;
    ::web::rtc::PeerConnectionInterface::RTCConfiguration rtc_config; rtc_config.sdp_semantics=::web::rtc::SdpSemantics::kUnifiedPlan; for(const auto& url:config.stun_servers){::web::rtc::PeerConnectionInterface::IceServer s;s.urls.push_back(url);rtc_config.servers.push_back(s);} if(!config.turn_url.empty()){::web::rtc::PeerConnectionInterface::IceServer s;s.urls.push_back(config.turn_url);s.username=config.turn_username;s.password=config.turn_password;rtc_config.servers.push_back(s);}
    ::web::rtc::PeerConnectionDependencies pdeps(impl_->observer.get()); auto result=impl_->factory->CreatePeerConnectionOrError(rtc_config,std::move(pdeps)); if(!result.ok())return false; impl_->pc=result.MoveValue();
    impl_->video_source=LocalVideoSource::Create(); impl_->audio_source=::rtc::make_ref_counted<LocalAudioSource>(); auto vt=impl_->factory->CreateVideoTrack(impl_->video_source,"luma-video"); auto at=impl_->factory->CreateAudioTrack("luma-audio",impl_->audio_source.get()); if(!vt||!at)return false; if(!impl_->pc->AddTrack(vt,{"luma-stream"}).ok()||!impl_->pc->AddTrack(at,{"luma-stream"}).ok())return false; return true;
}
bool NativeWebRtcPeerConnection::AddVideoFrame(const luma::client::media::pipeline::VideoFrame& f){
    if(!impl_->video_source||f.width==0||f.height==0||((f.width&1u)!=0)||((f.height&1u)!=0)) return false;
    const size_t y=f.width*f.height, uv=(f.width/2)*(f.height/2); auto b=::web::rtc::I420Buffer::Create(f.width,f.height);
    if(f.format==luma::client::media::pipeline::PixelFormat::I420){ if(f.data.size()<y+uv*2)return false; std::memcpy(b->MutableDataY(),f.data.data(),y); std::memcpy(b->MutableDataU(),f.data.data()+y,uv); std::memcpy(b->MutableDataV(),f.data.data()+y+uv,uv); }
    else if(f.format==luma::client::media::pipeline::PixelFormat::NV12){ if(f.data.size()<y+y/2)return false; std::memcpy(b->MutableDataY(),f.data.data(),y); const auto* src=f.data.data()+y; auto* u=b->MutableDataU(); auto* v=b->MutableDataV(); for(std::uint32_t row=0;row<f.height/2;++row) for(std::uint32_t col=0;col<f.width/2;++col){ const std::size_t i=row*f.width+col*2; u[row*b->StrideU()+col]=src[i]; v[row*b->StrideV()+col]=src[i+1]; } }
    else return false;
    impl_->video_source->Push(::web::rtc::VideoFrame::Builder().set_video_frame_buffer(b).set_timestamp_us(f.timestamp_us).build()); return true;
}
bool NativeWebRtcPeerConnection::AddAudioFrame(const luma::client::media::pipeline::AudioFrame& f){if(!impl_->audio_source||f.format!=luma::client::media::pipeline::AudioSampleFormat::S16||f.sample_rate<=0||f.channels==0)return false; const std::size_t bytes_per_sample=2; const std::size_t frames=f.data.size()/(bytes_per_sample*f.channels); if(frames==0)return false; impl_->audio_source->Push(f.data.data(),16,f.sample_rate,f.channels,frames); return true;}
bool NativeWebRtcPeerConnection::CreateOffer(){
    if(!impl_->pc) return false;
    auto obs=::rtc::make_ref_counted<DescriptionObserver>([this](auto*d){
        std::string sdp; if(!d->ToString(&sdp)) return;
        impl_->pc->SetLocalDescription(::rtc::make_ref_counted<SetObserver>([this,sdp](){ if(impl_->cb.on_local_description) impl_->cb.on_local_description("offer",sdp); },[](const std::string&){}),d);
    },[](const std::string&){});
    impl_->pc->CreateOffer(obs.get(),::web::rtc::RTCOfferAnswerOptions()); return true;
}
bool NativeWebRtcPeerConnection::CreateAnswer(){
    if(!impl_->pc) return false;
    auto obs=::rtc::make_ref_counted<DescriptionObserver>([this](auto*d){
        std::string sdp; if(!d->ToString(&sdp)) return;
        impl_->pc->SetLocalDescription(::rtc::make_ref_counted<SetObserver>([this,sdp](){ if(impl_->cb.on_local_description) impl_->cb.on_local_description("answer",sdp); },[](const std::string&){}),d);
    },[](const std::string&){});
    impl_->pc->CreateAnswer(obs.get(),::web::rtc::RTCOfferAnswerOptions()); return true;
}
bool NativeWebRtcPeerConnection::SetRemoteDescription(const std::string& type,const std::string&sdp){if(!impl_->pc)return false;::web::rtc::SdpType t; if(type=="offer")t=::web::rtc::SdpType::kOffer;else if(type=="answer")t=::web::rtc::SdpType::kAnswer;else return false;::web::rtc::SdpParseError e;auto d=::web::rtc::CreateSessionDescription(t,sdp,&e);if(!d)return false;impl_->pc->SetRemoteDescription(::rtc::make_ref_counted<SetObserver>([](){},[](const std::string&){}),d.release());return true;}
bool NativeWebRtcPeerConnection::AddRemoteIceCandidate(const std::string&mid,int mline,const std::string&candidate){if(!impl_->pc)return false;::web::rtc::SdpParseError e;std::unique_ptr<::web::rtc::IceCandidate> c(::web::rtc::CreateIceCandidate(mid,mline,candidate,&e));return c&&impl_->pc->AddIceCandidate(c.get());}
void NativeWebRtcPeerConnection::Close(){if(impl_&&impl_->pc){impl_->pc->Close();impl_->pc=nullptr;}if(impl_){impl_->factory=nullptr;impl_->video_source=nullptr;impl_->audio_source=nullptr;impl_->observer.reset();}}
bool NativeWebRtcPeerConnection::IsInitialized()const noexcept{return impl_&&impl_->pc!=nullptr;}
}
#else
namespace luma::client::webrtc { struct NativeWebRtcPeerConnection::Impl{}; NativeWebRtcPeerConnection::NativeWebRtcPeerConnection():impl_(std::make_unique<Impl>()){} NativeWebRtcPeerConnection::~NativeWebRtcPeerConnection()=default; bool NativeWebRtcPeerConnection::Initialize(const luma::contracts::PeerConnectionConfig&,WebRtcCallbacks){return false;} bool NativeWebRtcPeerConnection::AddVideoFrame(const luma::client::media::pipeline::VideoFrame&){return false;} bool NativeWebRtcPeerConnection::AddAudioFrame(const luma::client::media::pipeline::AudioFrame&){return false;} bool NativeWebRtcPeerConnection::CreateOffer(){return false;} bool NativeWebRtcPeerConnection::CreateAnswer(){return false;} bool NativeWebRtcPeerConnection::SetRemoteDescription(const std::string&,const std::string&){return false;} bool NativeWebRtcPeerConnection::AddRemoteIceCandidate(const std::string&,int,const std::string&){return false;} void NativeWebRtcPeerConnection::Close(){} bool NativeWebRtcPeerConnection::IsInitialized()const noexcept{return false;} }
#endif
