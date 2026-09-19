#include "NativeWebRtcPeerConnection.hpp"
#include <cassert>
#include <cstdint>
#include <utility>

int main() {
    using namespace luma::client::webrtc;
    using luma::client::media::pipeline::AudioFrame;
    using luma::client::media::pipeline::AudioSampleFormat;
    using luma::client::media::pipeline::PixelFormat;
    using luma::client::media::pipeline::VideoFrame;

    auto peer = std::make_shared<NativeWebRtcPeerConnection>();

    assert(!peer->IsInitialized());
    assert(!peer->CreateOffer());
    assert(!peer->CreateAnswer());

    AudioFrame invalid_audio;
    invalid_audio.format = AudioSampleFormat::S16;
    invalid_audio.sample_rate = 48000;
    invalid_audio.channels = 2;
    invalid_audio.data = {0, 0, 0};
    assert(!peer->AddAudioFrame(invalid_audio));

    VideoFrame invalid_video;
    invalid_video.width = 3;
    invalid_video.height = 2;
    invalid_video.format = PixelFormat::I420;
    invalid_video.data.resize(9);
    assert(!peer->AddVideoFrame(invalid_video));

    luma::contracts::PeerConnectionConfig config;
    WebRtcCallbacks callbacks;
    assert(peer->Initialize(config, std::move(callbacks)));
    assert(peer->IsInitialized());
    assert(peer->CreateOffer());

    peer->Close();
    assert(!peer->IsInitialized());
    assert(!peer->CreateOffer());

    return 0;
}
