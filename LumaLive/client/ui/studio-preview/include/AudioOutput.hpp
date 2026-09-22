#pragma once
#include "PreviewMedia.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <list>

namespace luma::client::ui::preview {
// Bounded PCM output, serialized with reset/close. Headers and PCM remain owned
// until the driver marks WHDR_DONE; reset returns all buffers before destruction.
class AudioOutput {
    struct Buffer { WAVEHDR header{}; std::vector<uint8_t> bytes; };
    std::mutex mutex_;
    HWAVEOUT device_{};
    std::list<Buffer> queue_;
    uint32_t rate_{};
    uint16_t channels_{};
    float volume_{.65f};
    void CloseLocked() {
        if(device_) {
            waveOutReset(device_);
            for(auto& b:queue_) waveOutUnprepareHeader(device_,&b.header,sizeof(WAVEHDR));
            waveOutClose(device_);device_=nullptr;
        }
        queue_.clear();
    }
public:
    ~AudioOutput(){Close();}
    void Close(){std::lock_guard lock(mutex_);CloseLocked();}
    void SetVolume(float v){std::lock_guard lock(mutex_);volume_=std::clamp(v,0.f,1.f);}
    bool Push(const AudioFrame& f) {
        if(!f.IsValid() || f.format!=AudioSampleFormat::S16 || f.channels>2 || f.sample_rate>192000) return false;
        std::lock_guard lock(mutex_);
        if(device_ && (rate_!=f.sample_rate || channels_!=f.channels)) CloseLocked();
        if(!device_) {
            WAVEFORMATEX fmt{};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=f.channels;
            fmt.nSamplesPerSec=f.sample_rate;fmt.wBitsPerSample=16;fmt.nBlockAlign=f.channels*2;
            fmt.nAvgBytesPerSec=fmt.nSamplesPerSec*fmt.nBlockAlign;
            if(waveOutOpen(&device_,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){device_=nullptr;return false;}
            rate_=f.sample_rate;channels_=f.channels;
        }
        for(auto it=queue_.begin();it!=queue_.end();) {
            if(it->header.dwFlags&WHDR_DONE){waveOutUnprepareHeader(device_,&it->header,sizeof(WAVEHDR));it=queue_.erase(it);}else ++it;
        }
        size_t queued=0;for(const auto& b:queue_)queued+=b.bytes.size();
        // At most 250ms of live PCM; a slow output must not grow unbounded.
        if(queued+f.data.size()>size_t(rate_)*channels_/2) return false;
        auto& b=queue_.emplace_back();b.bytes=f.data;
        for(size_t i=0;i<b.bytes.size();i+=2){int16_t s;std::memcpy(&s,b.bytes.data()+i,2);s=static_cast<int16_t>(s*volume_);std::memcpy(b.bytes.data()+i,&s,2);}
        b.header.lpData=reinterpret_cast<LPSTR>(b.bytes.data());b.header.dwBufferLength=static_cast<DWORD>(b.bytes.size());
        if(waveOutPrepareHeader(device_,&b.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){queue_.pop_back();return false;}
        if(waveOutWrite(device_,&b.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){waveOutUnprepareHeader(device_,&b.header,sizeof(WAVEHDR));queue_.pop_back();return false;}
        return true;
    }
};
}
