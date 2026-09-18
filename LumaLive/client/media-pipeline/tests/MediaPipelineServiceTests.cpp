#include "IMediaPipelineService.hpp"
#include <cassert>
#include <memory>

// The concrete service is intentionally kept private to the translation unit in this
// phase. These tests validate the public contract independently of construction.
int main() {
    using namespace luma::client::media::pipeline;

    VideoFrame video;
    assert(!video.IsValid());
    video.width = 1280;
    video.height = 720;
    video.data.resize(1280 * 720 * 4);
    assert(video.IsValid());

    AudioFrame audio;
    assert(!audio.IsValid());
    audio.data.resize(480 * 2 * sizeof(float));
    assert(audio.IsValid());

    MediaPipelineStats stats{};
    assert(stats.video_frames == 0);
    assert(stats.audio_frames == 0);

    return 0;
}
