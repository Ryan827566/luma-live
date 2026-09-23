#include "NativeWebRtcPeerConnection.hpp"
#include <iostream>
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Check failed: " #condition << " at line " << __LINE__ << "\n"; return 1; } } while (false)
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

    CHECK(!peer->IsInitialized());
    CHECK(!peer->CreateOffer());
    CHECK(!peer->CreateAnswer());

    AudioFrame invalid_audio;
    invalid_audio.format = AudioSampleFormat::S16;
    invalid_audio.sample_rate = 48000;
    invalid_audio.channels = 2;
    invalid_audio.data = {0, 0, 0};
    CHECK(!peer->AddAudioFrame(invalid_audio));

    VideoFrame invalid_video;
    invalid_video.width = 3;
    invalid_video.height = 2;
    invalid_video.format = PixelFormat::I420;
    invalid_video.data.resize(9);
    CHECK(!peer->AddVideoFrame(invalid_video));

    luma::contracts::PeerConnectionConfig config;
    WebRtcCallbacks callbacks;
    CHECK(peer->Initialize(config, std::move(callbacks)));
    CHECK(peer->IsInitialized());
    CHECK(peer->CreateOffer());

    peer->Close();
    CHECK(!peer->IsInitialized());
    CHECK(!peer->CreateOffer());

    // Regression: concurrent frame submission must not race the shared
    // LocalAudioSource sink collection.
    auto concurrent_peer = NativeWebRtcPeerConnection::Create();
    CHECK(concurrent_peer->Initialize(config, {}));
    AudioFrame valid_audio;
    valid_audio.format = AudioSampleFormat::S16;
    valid_audio.sample_rate = 48000;
    valid_audio.channels = 2;
    valid_audio.data.resize(480 * 2 * sizeof(std::int16_t));
    std::atomic<bool> frames_accepted{true};
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            for (int n = 0; n < 100; ++n) {
                if (!concurrent_peer->AddAudioFrame(valid_audio)) frames_accepted = false;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(frames_accepted.load());
    concurrent_peer->Close();

    // Regression: an asynchronous offer callback must not dereference the
    // peer after Close() releases the PeerConnection.
    auto async_peer = NativeWebRtcPeerConnection::Create();
    CHECK(async_peer->Initialize(config, {}));
    CHECK(async_peer->CreateOffer());
    async_peer->Close();

    return 0;
}
