#pragma once
#include "MediaTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace luma::client::ui::preview {
using media::pipeline::VideoFrame;
using media::pipeline::AudioFrame;
using media::pipeline::PixelFormat;
using media::pipeline::AudioSampleFormat;

// Frames at the UI boundary are tightly packed, top-down BGRA. Reject malformed
// input before allocating or indexing planes; no device-owned memory escapes.
inline std::shared_ptr<VideoFrame> ToBgra(const VideoFrame& f) {
    if (!f.width || !f.height || f.width > 8192 || f.height > 8192) return {};
    const size_t pixels = size_t(f.width) * f.height;
    const bool yuv = f.format == PixelFormat::NV12 || f.format == PixelFormat::I420;
    if (yuv && ((f.width | f.height) & 1)) return {};
    if (f.data.size() < (yuv ? pixels * 3 / 2 : pixels * 4)) return {};
    auto out = std::make_shared<VideoFrame>();
    out->width=f.width; out->height=f.height; out->timestamp_us=f.timestamp_us;
    out->format=PixelFormat::BGRA; out->data.resize(pixels*4);
    for(size_t y=0;y<f.height;++y) for(size_t x=0;x<f.width;++x) {
        const size_t i=y*f.width+x, d=i*4;
        if(!yuv) {
            if(f.format!=PixelFormat::BGRA && f.format!=PixelFormat::RGBA) return {};
            out->data[d]=f.data[d+(f.format==PixelFormat::RGBA?2:0)];
            out->data[d+1]=f.data[d+1];
            out->data[d+2]=f.data[d+(f.format==PixelFormat::RGBA?0:2)];
        } else {
            const size_t c=(y/2)*(f.width/2)+x/2;
            const int u=int(f.data[pixels+(f.format==PixelFormat::NV12?c*2:c)])-128;
            const int v=int(f.data[pixels+(f.format==PixelFormat::NV12?c*2+1:pixels/4+c)])-128;
            const int l=std::max(0,int(f.data[i])-16)*298;
            out->data[d]=static_cast<uint8_t>(std::clamp((l+516*u+128)>>8,0,255));
            out->data[d+1]=static_cast<uint8_t>(std::clamp((l-100*u-208*v+128)>>8,0,255));
            out->data[d+2]=static_cast<uint8_t>(std::clamp((l+409*v+128)>>8,0,255));
        }
        out->data[d+3]=255;
    }
    return out;
}

inline float Peak(const AudioFrame& f) {
    if(!f.IsValid() || f.format!=AudioSampleFormat::S16) return 0;
    int peak=0;
    for(size_t i=0;i<f.data.size();i+=2) {
        int16_t sample; std::memcpy(&sample,f.data.data()+i,2);
        peak=std::max(peak,std::abs(int(sample)));
    }
    return float(peak)/32768.f;
}

// A single-slot mailbox bounds memory and latency when capture outpaces paint.
class VideoMailbox {
    mutable std::mutex mutex_;
    std::shared_ptr<VideoFrame> frame_;
public:
    void Put(const VideoFrame& f) { auto next=ToBgra(f); if(next){std::lock_guard lock(mutex_);frame_=std::move(next);} }
    std::shared_ptr<const VideoFrame> Get() const {std::lock_guard lock(mutex_);return frame_;}
    void Clear() {std::lock_guard lock(mutex_);frame_.reset();}
};
}
