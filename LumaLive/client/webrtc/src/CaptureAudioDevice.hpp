#pragma once
#include "modules/audio_device/include/audio_device.h"
#include "MediaTypes.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace luma::client::webrtc {
// Media Foundation owns the selected microphone. Feed its PCM through the ADM
// transport, rather than AudioSource::AddSink (a receive-side observer API).
// Pulling playout every 10ms also drives the remote track sinks. The client owns
// actual speaker output, so WebRTC must not open a second speaker device.
class CaptureAudioDevice:public ::webrtc::AudioDeviceModule {
    std::atomic<bool> initialized_{false},recording_{false},playing_{false};
    std::mutex transportMutex_,queueMutex_;
    ::webrtc::AudioTransport* transport_{};
    std::deque<int16_t> samples_;
    std::thread clock_;
public:
    ~CaptureAudioDevice()override{Terminate();}
    bool Push(const media::pipeline::AudioFrame& f){
        if(f.format!=media::pipeline::AudioSampleFormat::S16||f.sample_rate!=48000||!f.IsValid()||f.channels>2)return false;
        std::lock_guard lock(queueMutex_);
        const size_t count=f.data.size()/(2*f.channels);
        if(samples_.size()+count>9600)samples_.clear();
        for(size_t i=0;i<count;++i){int sum=0;for(size_t c=0;c<f.channels;++c){int16_t s;std::memcpy(&s,f.data.data()+(i*f.channels+c)*2,2);sum+=s;}samples_.push_back(static_cast<int16_t>(sum/f.channels));}
        return true;
    }
    int32_t ActiveAudioLayer(AudioLayer* layer)const override{if(!layer)return -1;*layer=kDummyAudio;return 0;}
    int32_t RegisterAudioCallback(::webrtc::AudioTransport* t)override{std::lock_guard lock(transportMutex_);transport_=t;return 0;}
    int32_t Init()override{
        if(initialized_.exchange(true))return 0;
        clock_=std::thread([this]{
            auto next=std::chrono::steady_clock::now();
            while(initialized_){
                next+=std::chrono::milliseconds(10);std::array<int16_t,480> in{};std::array<int16_t,960> out{};
                {std::lock_guard lock(queueMutex_);for(auto& s:in){if(samples_.empty())break;s=samples_.front();samples_.pop_front();}}
                {std::lock_guard lock(transportMutex_);if(transport_){
                    if(recording_){uint32_t level=0;transport_->RecordedDataIsAvailable(in.data(),480,2,1,48000,0,0,0,false,level);}
                    if(playing_){size_t count=0;int64_t elapsed=0,ntp=0;transport_->NeedMorePlayData(480,2,2,48000,out.data(),count,&elapsed,&ntp);}
                }}
                if(next<std::chrono::steady_clock::now()-std::chrono::milliseconds(20))next=std::chrono::steady_clock::now();
                std::this_thread::sleep_until(next);
            }
        });return 0;
    }
    int32_t Terminate()override{initialized_=false;recording_=false;playing_=false;if(clock_.joinable())clock_.join();std::lock_guard lock(queueMutex_);samples_.clear();return 0;}
    bool Initialized()const override{return initialized_;}
    int16_t PlayoutDevices()override{return 1;}int16_t RecordingDevices()override{return 1;}
    int32_t PlayoutDeviceName(uint16_t i,char* n,char* g)override{if(i||!n||!g)return -1;std::strcpy(n,"LumaLive client output");g[0]=0;return 0;}
    int32_t RecordingDeviceName(uint16_t i,char* n,char* g)override{return PlayoutDeviceName(i,n,g);}
    int32_t SetPlayoutDevice(uint16_t i)override{return i?-1:0;}int32_t SetPlayoutDevice(WindowsDeviceType)override{return 0;}
    int32_t SetRecordingDevice(uint16_t i)override{return i?-1:0;}int32_t SetRecordingDevice(WindowsDeviceType)override{return 0;}
    int32_t PlayoutIsAvailable(bool* v)override{*v=true;return 0;}int32_t RecordingIsAvailable(bool* v)override{*v=true;return 0;}
    int32_t InitPlayout()override{return 0;}int32_t InitRecording()override{return 0;}
    bool PlayoutIsInitialized()const override{return initialized_;}bool RecordingIsInitialized()const override{return initialized_;}
    int32_t StartPlayout()override{playing_=true;return 0;}int32_t StopPlayout()override{playing_=false;return 0;}bool Playing()const override{return playing_;}
    int32_t StartRecording()override{recording_=true;return 0;}int32_t StopRecording()override{recording_=false;return 0;}bool Recording()const override{return recording_;}
    int32_t InitSpeaker()override{return 0;}bool SpeakerIsInitialized()const override{return initialized_;}
    int32_t InitMicrophone()override{return 0;}bool MicrophoneIsInitialized()const override{return initialized_;}
    int32_t SpeakerVolumeIsAvailable(bool* v)override{*v=false;return 0;}int32_t MicrophoneVolumeIsAvailable(bool* v)override{*v=false;return 0;}
    int32_t SetSpeakerVolume(uint32_t)override{return -1;}int32_t SetMicrophoneVolume(uint32_t)override{return -1;}
    int32_t SpeakerVolume(uint32_t* v)const override{*v=255;return 0;}int32_t MicrophoneVolume(uint32_t* v)const override{*v=255;return 0;}
    int32_t MaxSpeakerVolume(uint32_t* v)const override{*v=255;return 0;}int32_t MaxMicrophoneVolume(uint32_t* v)const override{*v=255;return 0;}
    int32_t MinSpeakerVolume(uint32_t* v)const override{*v=0;return 0;}int32_t MinMicrophoneVolume(uint32_t* v)const override{*v=0;return 0;}
    int32_t SpeakerMuteIsAvailable(bool* v)override{*v=false;return 0;}int32_t MicrophoneMuteIsAvailable(bool* v)override{*v=false;return 0;}
    int32_t SetSpeakerMute(bool)override{return -1;}int32_t SetMicrophoneMute(bool)override{return -1;}
    int32_t SpeakerMute(bool* v)const override{*v=false;return 0;}int32_t MicrophoneMute(bool* v)const override{*v=false;return 0;}
    int32_t StereoPlayoutIsAvailable(bool* v)const override{*v=true;return 0;}int32_t StereoRecordingIsAvailable(bool* v)const override{*v=false;return 0;}
    int32_t SetStereoPlayout(bool)override{return 0;}int32_t SetStereoRecording(bool v)override{return v?-1:0;}
    int32_t StereoPlayout(bool* v)const override{*v=true;return 0;}int32_t StereoRecording(bool* v)const override{*v=false;return 0;}
    int32_t PlayoutDelay(uint16_t* v)const override{*v=0;return 0;}
    bool BuiltInAECIsAvailable()const override{return false;}bool BuiltInAGCIsAvailable()const override{return false;}bool BuiltInNSIsAvailable()const override{return false;}
    int32_t EnableBuiltInAEC(bool)override{return -1;}int32_t EnableBuiltInAGC(bool)override{return -1;}int32_t EnableBuiltInNS(bool)override{return -1;}
};
}
