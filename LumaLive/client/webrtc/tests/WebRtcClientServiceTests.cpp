#include "NativeWebRtcPeerConnection.hpp"
#include <cassert>
#include <cstdint>
#include <utility>
#include <atomic>
#include <thread>
#include <vector>

int main() {
    using namespace luma::client::webrtc;
    using luma::client::media::pipeline::AudioFrame;
    using luma::client::media::pipeline::AudioSampleFormat;
    using luma::client::media::pipeline::PixelFormat;
    using luma::client::media::pipeline::VideoFrame;

    auto peer = NativeWebRtcPeerConnection::Create();

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

    // Regression: concurrent frame submission must not race the shared
    // LocalAudioSource sink collection.
    auto concurrent_peer = NativeWebRtcPeerConnection::Create();
    assert(concurrent_peer->Initialize(config, {}));
    AudioFrame valid_audio;
    valid_audio.format = AudioSampleFormat::S16;
    valid_audio.sample_rate = 48000;
    valid_audio.channels = 2;
    valid_audio.data.resize(480 * 2 * sizeof(std::int16_t));
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            for (int n = 0; n < 100; ++n) {
                assert(concurrent_peer->AddAudioFrame(valid_audio));
            }
        });
    }
    for (auto& worker : workers) worker.join();
    concurrent_peer->Close();

    // Regression: an asynchronous offer callback must not dereference the
    // peer after Close() releases the PeerConnection.
    auto async_peer = NativeWebRtcPeerConnection::Create();
    assert(async_peer->Initialize(config, {}));
    assert(async_peer->CreateOffer());
    async_peer->Close();

    return 0;
}
