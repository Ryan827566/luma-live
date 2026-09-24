#pragma once
#include "MediaTypes.hpp"
#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
namespace luma::client::ui::preview {
// The output device calls Pull every 10 ms. Incoming tracks are aligned to that
// shared playout clock, never concatenated. Network jitter stays in WebRTC.
class MeetingAudioMixer {
public:
 using Audio=media::pipeline::AudioFrame;
 bool Push(const std::string& peer,const Audio& frame){
  if(peer.empty()||frame.format!=media::pipeline::AudioSampleFormat::S16||frame.sample_rate!=48000||(frame.channels!=1&&frame.channels!=2)||frame.data.empty()||frame.data.size()%(2*frame.channels)!=0||frame.data.size()>38400)return false;
  std::lock_guard lock(mutex_);if(!queues_.count(peer)&&queues_.size()>=5)return false;auto& queue=queues_[peer];
  for(size_t at=0;at<frame.data.size();at+=2*frame.channels){int sum=0;for(int channel=0;channel<frame.channels;++channel){int16_t value;std::memcpy(&value,frame.data.data()+at+2*channel,2);sum+=value;}queue.push_back(static_cast<int16_t>(sum/frame.channels));}
  // Limit retained decoded audio to 100 ms per participant.
  while(queue.size()>4800)queue.pop_front();return true;
 }
 Audio Pull(){std::lock_guard lock(mutex_);Audio out;out.format=media::pipeline::AudioSampleFormat::S16;out.sample_rate=48000;out.channels=1;out.timestamp_us=timestamp_;timestamp_+=10000;out.data.resize(960);
  for(size_t sample=0;sample<480;++sample){int mixed=0;for(auto& [peer,queue]:queues_)if(!queue.empty()){mixed+=queue.front();queue.pop_front();}const auto value=static_cast<int16_t>(std::clamp(mixed,-32768,32767));std::memcpy(out.data.data()+2*sample,&value,2);}return out;
 }
 void Remove(const std::string& peer){std::lock_guard lock(mutex_);queues_.erase(peer);}
 void Clear(){std::lock_guard lock(mutex_);queues_.clear();timestamp_=0;}
private:
 std::mutex mutex_;std::map<std::string,std::deque<int16_t>> queues_;std::uint64_t timestamp_{0};
};
}
