#include "NativeWebRtcPeerConnection.hpp"

#if defined(LUMALIVE_HAS_WEBRTC)
#include "api/media_stream_interface.h"
#include "api/notifier.h"
#include "api/scoped_refptr.h"
#include "api/peer_connection_interface.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/jsep.h"
#include "media/base/video_broadcaster.h"
#include "rtc_base/ref_counted_object.h"
#include <atomic>
#include <mutex>
#include <set>
#include <vector>

namespace luma::client::webrtc {
namespace {

class LocalVideoSource : public ::webrtc::Notifier<::webrtc::VideoTrackSourceInterface> {
public:
    static ::rtc::scoped_refptr<LocalVideoSource> Create() { return ::rtc::scoped_refptr<LocalVideoSource>(new ::rtc::RefCountedObject<LocalVideoSource>()); }
    ::webrtc::MediaSourceInterface::SourceState state() const override { return state_; }
    bool remote() const override { return false; }
    void RegisterObserver(::webrtc::ObserverInterface* o) override { std::lock_guard lock(observer_mutex_); observers_.insert(o); }
    void UnregisterObserver(::webrtc::ObserverInterface* o) override { std::lock_guard lock(observer_mutex_); observers_.erase(o); }
    void AddOrUpdateSink(::rtc::VideoSinkInterface<::webrtc::VideoFrame>* sink, const ::rtc::VideoSinkWants& wants) override { broadcaster_.AddOrUpdateSink(sink, wants); }
    void RemoveSink(::rtc::VideoSinkInterface<::webrtc::VideoFrame>* sink) override { broadcaster_.RemoveSink(sink); }
    bool is_screencast() const override { return false; }
    std::optional<bool> needs_denoising() const override { return std::nullopt; }
    bool GetStats(::webrtc::VideoTrackSourceInterface::Stats* stats) override { if (!stats) return false; const int width=width_.load(std::memory_order_relaxed); const int height=height_.load(std::memory_order_relaxed); if (width==0 || height==0) return false; stats->input_width=width; stats->input_height=height; return true; }
    bool SupportsEncodedOutput() const override { return false; }
    void GenerateKeyFrame() override {}
    void AddEncodedSink(::rtc::VideoSinkInterface<::webrtc::RecordableEncodedFrame>*) override {}
    void RemoveEncodedSink(::rtc::VideoSinkInterface<::webrtc::RecordableEncodedFrame>*) override {}
    void Push(const ::webrtc::VideoFrame& frame) { width_.store(frame.width(), std::memory_order_relaxed); height_.store(frame.height(), std::memory_order_relaxed); broadcaster_.OnFrame(frame); }
private:
    ::rtc::VideoBroadcaster broadcaster_;
    ::webrtc::MediaSourceInterface::SourceState state_{::webrtc::MediaSourceInterface::kLive};
    std::mutex observer_mutex_;
    std::set<::webrtc::ObserverInterface*> observers_;
    std::atomic<int> width_{0};
    std::atomic<int> height_{0};
};

class LocalAudioSource : public ::webrtc::Notifier<::webrtc::AudioSourceInterface> {
public:
    ::webrtc::MediaSourceInterface::SourceState state() const override { return ::webrtc::MediaSourceInterface::kLive; }
    bool remote() const override { return false; }
    void RegisterObserver(::webrtc::ObserverInterface*) override {}
    void UnregisterObserver(::webrtc::ObserverInterface*) override {}
    void AddSink(::webrtc::AudioTrackSinkInterface* sink) override { std::lock_guard lock(m_); sinks_.insert(sink); }
    void RemoveSink(::webrtc::AudioTrackSinkInterface* sink) override { std::lock_guard lock(m_); sinks_.erase(sink); }
    void Push(const void* data,int bits,int rate,size_t channels,size_t frames){
        std::vector<::webrtc::AudioTrackSinkInterface*> sinks;
        { std::lock_guard lock(m_); sinks.assign(sinks_.begin(), sinks_.end()); }
        for(auto* s:sinks) if(s) s->OnData(data,bits,rate,channels,frames);
    }
private:
    std::mutex m_;
    std::set<::webrtc::AudioTrackSinkInterface*> sinks_;
};

class DescriptionObserver : public ::webrtc::CreateSessionDescriptionObserver {
public:
    explicit DescriptionObserver(std::function<void(::webrtc::SessionDescriptionInterface*)> ok,std::function<void(const std::string&)> fail):ok_(std::move(ok)),fail_(std::move(fail)){}
    void OnSuccess(::webrtc::SessionDescriptionInterface* d) override { ok_(d); }
    void OnFailure(::webrtc::RTCError e) override { fail_(e.message()); }
protected: ~DescriptionObserver() override = default;
private:
    std::function<void(::webrtc::SessionDescriptionInterface*)> ok_;
    std::function<void(const std::string&)> fail_;
};
class SetObserver : public ::webrtc::SetSessionDescriptionObserver {
public:
    explicit SetObserver(std::function<void()> ok,std::function<void(const std::string&)> fail):ok_(std::move(ok)),fail_(std::move(fail)){}
    void OnSuccess() override {ok_();}
    void OnFailure(::webrtc::RTCError e) override {fail_(e.message());}
protected: ~SetObserver() override = default;
private:
    std::function<void()> ok_;
    std::function<void(const std::string&)> fail_;
};

class PcObserver : public ::webrtc::PeerConnectionObserver {
public:
    explicit PcObserver(WebRtcCallbacks& cb):cb_(cb){}
    void OnSignalingChange(::webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(::rtc::scoped_refptr<::webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(::webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnConnectionChange(::webrtc::PeerConnectionInterface::PeerConnectionState state) override { if(cb_.on_connection_state) cb_.on_connection_state(std::string(::webrtc::PeerConnectionInterface::AsString(state))); }
    void OnIceCandidate(const ::webrtc::IceCandidateInterface* c) override { if(cb_.on_local_ice_candidate && c){ const std::string s = c->candidate().ToCandidateAttribute(true); cb_.on_local_ice_candidate(c->sdp_mid(), c->sdp_mline_index(), s); } }
    void OnTrack(::rtc::scoped_refptr<::webrtc::RtpTransceiverInterface> transceiver) override {
        if(!transceiver || !transceiver->receiver()) return; auto track=transceiver->receiver()->track(); if(!track) return;
        if(track->kind()==::webrtc::MediaStreamTrackInterface::kVideoKind){ auto video=static_cast<::webrtc::VideoTrackInterface*>(track.get()); video->AddOrUpdateSink(&video_sink_,::rtc::VideoSinkWants()); }
        else if(track->kind()==::webrtc::MediaStreamTrackInterface::kAudioKind){ auto audio=static_cast<::webrtc::AudioTrackInterface*>(track.get()); audio->AddSink(&audio_sink_); }
    }
private:
    class VideoSink final : public ::rtc::VideoSinkInterface<::webrtc::VideoFrame> { public: explicit VideoSink(WebRtcCallbacks& cb):cb_(cb){} void OnFrame(const ::webrtc::VideoFrame&) override { if(cb_.on_remote_video_frame) cb_.on_remote_video_frame(); } private: WebRtcCallbacks& cb_; };
    class AudioSink final : public ::webrtc::AudioTrackSinkInterface { public: explicit AudioSink(WebRtcCallbacks& cb):cb_(cb){} void OnData(const void*,int,int,size_t,size_t) override { if(cb_.on_remote_audio_frame) cb_.on_remote_audio_frame(); } private: WebRtcCallbacks& cb_; };
    WebRtcCallbacks& cb_; VideoSink video_sink_{cb_}; AudioSink audio_sink_{cb_};
};
}

struct NativeWebRtcPeerConnection::Impl {
    WebRtcCallbacks cb;
    std::unique_ptr<::rtc::Thread> network_thread;
    std::unique_ptr<::rtc::Thread> worker_thread;
    std::unique_ptr<::rtc::Thread> signaling_thread;
    ::rtc::scoped_refptr<::webrtc::PeerConnectionFactoryInterface> factory;
    ::rtc::scoped_refptr<::webrtc::PeerConnectionInterface> pc;
    ::rtc::scoped_refptr<LocalVideoSource> video_source;
    ::rtc::scoped_refptr<LocalAudioSource> audio_source;
    std::unique_ptr<PcObserver> observer;
};

std::shared_ptr<NativeWebRtcPeerConnection> NativeWebRtcPeerConnection::Create() { return std::shared_ptr<NativeWebRtcPeerConnection>(new NativeWebRtcPeerConnection()); }
NativeWebRtcPeerConnection::NativeWebRtcPeerConnection():impl_(std::make_unique<Impl>()){}
NativeWebRtcPeerConnection::~NativeWebRtcPeerConnection(){Close();}
bool NativeWebRtcPeerConnection::Initialize(const luma::contracts::PeerConnectionConfig& config, WebRtcCallbacks callbacks){
    if(impl_->pc) return false; impl_->cb=std::move(callbacks); impl_->observer=std::make_unique<PcObserver>(impl_->cb);
    impl_->network_thread=::rtc::Thread::CreateWithSocketServer();
    impl_->worker_thread=::rtc::Thread::Create();
    impl_->signaling_thread=::rtc::Thread::CreateWithSocketServer();
    if(!impl_->network_thread || !impl_->worker_thread || !impl_->signaling_thread) return false;
    if(!impl_->network_thread->Start() || !impl_->worker_thread->Start() || !impl_->signaling_thread->Start()) return false;

    impl_->factory=::webrtc::CreatePeerConnectionFactory(
        impl_->network_thread.get(),
        impl_->worker_thread.get(),
        impl_->signaling_thread.get(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);
    if(!impl_->factory) return false;
    ::webrtc::PeerConnectionInterface::RTCConfiguration rtc_config; rtc_config.sdp_semantics=::webrtc::SdpSemantics::kUnifiedPlan; for(const auto& url:config.stun_servers){::webrtc::PeerConnectionInterface::IceServer s;s.urls.push_back(url);rtc_config.servers.push_back(s);} if(!config.turn_url.empty()){::webrtc::PeerConnectionInterface::IceServer s;s.urls.push_back(config.turn_url);s.username=config.turn_username;s.password=config.turn_password;rtc_config.servers.push_back(s);}
    ::webrtc::PeerConnectionDependencies pdeps(impl_->observer.get());
    auto result=impl_->factory->CreatePeerConnectionOrError(rtc_config,std::move(pdeps));
    if(!result.ok()) return false;

    auto pc=result.MoveValue();
    auto video_source=LocalVideoSource::Create();
    auto audio_source=::rtc::scoped_refptr<LocalAudioSource>(
        new ::rtc::RefCountedObject<LocalAudioSource>());
    auto vt=impl_->factory->CreateVideoTrack(video_source,"luma-video");
    auto at=impl_->factory->CreateAudioTrack("luma-audio",audio_source.get());
    if(!vt||!at) return false;
    if(!pc->AddTrack(vt,{"luma-stream"}).ok() ||
       !pc->AddTrack(at,{"luma-stream"}).ok()) return false;

    impl_->pc=std::move(pc);
    impl_->video_source=std::move(video_source);
    impl_->audio_source=std::move(audio_source);
    return true;
}
bool NativeWebRtcPeerConnection::AddVideoFrame(const luma::client::media::pipeline::VideoFrame& f){
    if(!impl_->video_source||f.width==0||f.height==0||((f.width&1u)!=0)||((f.height&1u)!=0)) return false;
    const size_t y=f.width*f.height, uv=(f.width/2)*(f.height/2); auto b=::webrtc::I420Buffer::Create(f.width,f.height);
    if(f.format==luma::client::media::pipeline::PixelFormat::I420){ if(f.data.size()<y+uv*2)return false; std::memcpy(b->MutableDataY(),f.data.data(),y); std::memcpy(b->MutableDataU(),f.data.data()+y,uv); std::memcpy(b->MutableDataV(),f.data.data()+y+uv,uv); }
    else if(f.format==luma::client::media::pipeline::PixelFormat::NV12){ if(f.data.size()<y+y/2)return false; std::memcpy(b->MutableDataY(),f.data.data(),y); const auto* src=f.data.data()+y; auto* u=b->MutableDataU(); auto* v=b->MutableDataV(); for(std::uint32_t row=0;row<f.height/2;++row) for(std::uint32_t col=0;col<f.width/2;++col){ const std::size_t i=row*f.width+col*2; u[row*b->StrideU()+col]=src[i]; v[row*b->StrideV()+col]=src[i+1]; } }
    else return false;
    impl_->video_source->Push(::webrtc::VideoFrame::Builder().set_video_frame_buffer(b).set_timestamp_us(f.timestamp_us).build()); return true;
}
bool NativeWebRtcPeerConnection::AddAudioFrame(const luma::client::media::pipeline::AudioFrame& f){
    if(!impl_->audio_source||f.format!=luma::client::media::pipeline::AudioSampleFormat::S16||f.sample_rate<=0||f.channels==0)return false;
    constexpr std::size_t bytes_per_sample=2;
    const std::size_t frame_bytes=bytes_per_sample*static_cast<std::size_t>(f.channels);
    if(frame_bytes==0||f.data.empty()||f.data.size()%frame_bytes!=0)return false;
    const std::size_t frames=f.data.size()/frame_bytes;
    impl_->audio_source->Push(f.data.data(),16,f.sample_rate,f.channels,frames); return true;
}
bool NativeWebRtcPeerConnection::CreateOffer(){
    if(!impl_->pc) return false;
    auto weak=weak_from_this();
    auto* obs = new ::rtc::RefCountedObject<DescriptionObserver>(
            [weak](auto* d){
                auto self=weak.lock(); if(!self||!d)return;
                std::string sdp;
                if(!d->ToString(&sdp)) return;
                self->impl_->pc->SetLocalDescription(
                    new ::rtc::RefCountedObject<SetObserver>(
                            [weak,sdp](){
                                auto self=weak.lock(); if(!self)return;
                                if(self->impl_->cb.on_local_description)
                                    self->impl_->cb.on_local_description("offer",sdp);
                            },
                            [](const std::string&){}),
                    d);
            },
            [](const std::string&){});
    impl_->pc->CreateOffer(obs,::webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    return true;
}

bool NativeWebRtcPeerConnection::CreateAnswer(){
    if(!impl_->pc) return false;
    auto weak=weak_from_this();
    auto* obs = new ::rtc::RefCountedObject<DescriptionObserver>(
            [weak](auto* d){
                auto self=weak.lock(); if(!self||!d)return;
                std::string sdp;
                if(!d->ToString(&sdp)) return;
                self->impl_->pc->SetLocalDescription(
                    new ::rtc::RefCountedObject<SetObserver>(
                            [weak,sdp](){
                                auto self=weak.lock(); if(!self)return;
                                if(self->impl_->cb.on_local_description)
                                    self->impl_->cb.on_local_description("answer",sdp);
                            },
                            [](const std::string&){}),
                    d);
            },
            [](const std::string&){});
    impl_->pc->CreateAnswer(obs,::webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    return true;
}

bool NativeWebRtcPeerConnection::SetRemoteDescription(
    const std::string& type,const std::string&sdp){
    if(!impl_->pc) return false;
    ::webrtc::SdpType t;
    if(type=="offer") t=::webrtc::SdpType::kOffer;
    else if(type=="answer") t=::webrtc::SdpType::kAnswer;
    else return false;
    ::webrtc::SdpParseError e;
    auto d=::webrtc::CreateSessionDescription(t,sdp,&e);
    if(!d) return false;
    auto weak=weak_from_this();
    impl_->pc->SetRemoteDescription(
        new ::rtc::RefCountedObject<SetObserver>(
            [](){},
            [weak](const std::string& message){
                auto self=weak.lock();
                if(!self) return;
                if(self->impl_->cb.on_connection_state)
                    self->impl_->cb.on_connection_state("remote_description_error:" + message);
            }),
        d.release());
    return true;
}

bool NativeWebRtcPeerConnection::AddRemoteIceCandidate(const std::string&mid,int mline,const std::string&candidate){if(!impl_->pc)return false;::webrtc::SdpParseError e;std::unique_ptr<::webrtc::IceCandidateInterface> c(::webrtc::CreateIceCandidate(mid,mline,candidate,&e));return c&&impl_->pc->AddIceCandidate(c.get());}
void NativeWebRtcPeerConnection::Close(){if(impl_&&impl_->pc){impl_->pc->Close();impl_->pc=nullptr;}if(impl_){impl_->factory=nullptr;impl_->video_source=nullptr;impl_->audio_source=nullptr;impl_->observer.reset();if(impl_->signaling_thread){impl_->signaling_thread->Stop();impl_->signaling_thread.reset();}if(impl_->worker_thread){impl_->worker_thread->Stop();impl_->worker_thread.reset();}if(impl_->network_thread){impl_->network_thread->Stop();impl_->network_thread.reset();}}}
bool NativeWebRtcPeerConnection::IsInitialized()const noexcept{return impl_&&impl_->pc!=nullptr;}
}
#else
namespace luma::client::webrtc { struct NativeWebRtcPeerConnection::Impl{}; std::shared_ptr<NativeWebRtcPeerConnection> NativeWebRtcPeerConnection::Create() { return std::shared_ptr<NativeWebRtcPeerConnection>(new NativeWebRtcPeerConnection()); } NativeWebRtcPeerConnection::NativeWebRtcPeerConnection():impl_(std::make_unique<Impl>()){} NativeWebRtcPeerConnection::~NativeWebRtcPeerConnection()=default; bool NativeWebRtcPeerConnection::Initialize(const luma::contracts::PeerConnectionConfig&,WebRtcCallbacks){return false;} bool NativeWebRtcPeerConnection::AddVideoFrame(const luma::client::media::pipeline::VideoFrame&){return false;} bool NativeWebRtcPeerConnection::AddAudioFrame(const luma::client::media::pipeline::AudioFrame&){return false;} bool NativeWebRtcPeerConnection::CreateOffer(){return false;} bool NativeWebRtcPeerConnection::CreateAnswer(){return false;} bool NativeWebRtcPeerConnection::SetRemoteDescription(const std::string&,const std::string&){return false;} bool NativeWebRtcPeerConnection::AddRemoteIceCandidate(const std::string&,int,const std::string&){return false;} void NativeWebRtcPeerConnection::Close(){} bool NativeWebRtcPeerConnection::IsInitialized()const noexcept{return false;} }
#endif
