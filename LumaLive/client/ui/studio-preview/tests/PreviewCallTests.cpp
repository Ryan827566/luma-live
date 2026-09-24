#include "PreviewCall.hpp"
#include "PreviewMedia.hpp"
#include "TcpSignalingServer.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <windows.h>
#include <objbase.h>
using namespace luma::client::ui::preview;
using namespace std::chrono_literals;
namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Predicate>
void Until(std::initializer_list<PreviewCall*> calls, Predicate ready,
           std::chrono::milliseconds timeout = 3s) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    do {
        for (auto* call : calls) {
            const auto status = call->Poll();
            if (!status.empty()) std::cout << status << '\n';
        }
        if (ready()) return;
        std::this_thread::sleep_for(10ms);
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Timed out waiting for call lifecycle state");
}
}
int main() {
    std::cout << std::unitbuf;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int result = 0;
    try {
        luma::server::signaling::TcpSignalingServer server;
        uint16_t port = 19000;
        while (port < 19100 && !server.Start(port)) ++port;
        Require(port < 19100, "No test signaling port");
        PreviewCall a, b, observer;
        std::atomic<int> av{0}, bv{0}, aa{0}, ba{0}, loudA{0}, loudB{0}, leaked{0};
        luma::client::webrtc::WebRtcCallbacks ca, cb, co;
        ca.on_remote_video = [&](auto f) { if (ToBgra(f)) ++av; };
        cb.on_remote_video = [&](auto f) { if (ToBgra(f)) ++bv; };
        ca.on_remote_audio = [&](auto f) { ++aa; if (Peak(f) > .02f) ++loudA; };
        cb.on_remote_audio = [&](auto f) { ++ba; if (Peak(f) > .02f) ++loudB; };
        co.on_remote_video = [&](auto) { ++leaked; };
        co.on_remote_audio = [&](auto) { ++leaked; };
        Require(a.Start("127.0.0.1", port, "rtc-test", "a", ca, {}), "A startup failed");
        Require(b.Start("127.0.0.1", port, "rtc-test", "b", cb, {}), "B startup failed");
        Require(observer.Start("127.0.0.1", port, "rtc-test", "observer", co, {}), "Observer startup failed");
        Until({&a, &b, &observer}, [&] {
            return a.State() == CallState::Ready && b.State() == CallState::Ready &&
                observer.State() == CallState::Ready && a.Participants().size() == 2 &&
                b.Participants().size() == 2 && observer.Participants().size() == 2;
        });
        Require(a.Remote().empty() && b.Remote().empty(), "Joining unexpectedly started a call");
        Require(!a.Call("missing") && !a.Call("a"), "Invalid target accepted");
        Require(a.Call("b"), "Invite failed");
        Until({&a, &b, &observer}, [&] { return b.State() == CallState::Incoming; });
        Require(b.Reject(), "Reject failed");
        Until({&a, &b, &observer}, [&] { return a.State() == CallState::Ready; });
        Require(a.LastStatus().find("rejected") != std::string::npos, "Rejection status lost");
        Require(a.Call("b"), "Second invite failed");
        Until({&a, &b, &observer}, [&] { return b.State() == CallState::Incoming; });
        Require(a.Hangup(), "Cancel failed");
        Until({&a, &b, &observer}, [&] { return b.State() == CallState::Ready; });
        Require(b.LastStatus().find("cancelled") != std::string::npos, "Cancellation status lost");

        Require(a.Call("b"), "Accepted invite failed");
        Until({&a, &b, &observer}, [&] { return b.State() == CallState::Incoming; });
        Require(observer.Call("b"), "Busy test invite failed");
        Until({&a, &b, &observer}, [&] { return observer.State() == CallState::Ready; });
        Require(observer.LastStatus().find("busy") != std::string::npos, "Busy response missing");
        Require(b.Accept(), "Accept failed");
        VideoFrame video; video.width = 320; video.height = 240;
        video.format = PixelFormat::I420; video.data.resize(320 * 240 * 3 / 2, 128);
        AudioFrame audio; audio.channels = 1; audio.sample_rate = 48000;
        audio.format = AudioSampleFormat::S16; audio.data.resize(960);
        const auto start = std::chrono::steady_clock::now(); int tick = 0;
        while (std::chrono::steady_clock::now() - start < 15s) {
            for (auto* call : {&a, &b, &observer}) call->Poll();
            if (tick % 3 == 0) {
                video.timestamp_us = static_cast<uint64_t>(tick) * 10000;
                std::fill(video.data.begin(), video.data.begin() + 320 * 240, static_cast<uint8_t>(32 + tick % 160));
                a.Video(video); b.Video(video);
            }
            for (int i = 0; i < 480; ++i) {
                int16_t sample = static_cast<int16_t>(std::sin((tick * 480 + i) * 440. * 6.283185307 / 48000) * 10000);
                std::memcpy(audio.data.data() + i * 2, &sample, 2);
            }
            a.Audio(audio); b.Audio(audio); ++tick;
            if (av > 15 && bv > 15 && loudA > 25 && loudB > 25 && a.DurationSeconds() >= 1) break;
            std::this_thread::sleep_for(10ms);
        }
        std::cout << "Decoded video A/B=" << av << '/' << bv << ", audio=" << aa << '/' << ba
                  << ", non-silent=" << loudA << '/' << loudB << std::endl;
        Require(av > 15 && bv > 15 && loudA > 25 && loudB > 25, "Bidirectional decoded media missing");
        Require(a.State() == CallState::Connected && b.State() == CallState::Connected, "Connected state missing");
        Require(a.DurationSeconds() >= 1, "Call duration did not advance");
        Require(leaked == 0 && observer.Remote().empty(), "Media leaked to uninvolved participant");
        Require(a.Hangup(), "Hangup failed");
        Until({&a, &b, &observer}, [&] { return b.State() == CallState::Ready; });
        Require(a.Remote().empty() && b.Remote().empty(), "Hangup retained remote identity");
        Require(b.Call("a"), "Reverse call failed");
        Until({&a, &b, &observer}, [&] { return a.State() == CallState::Incoming; });
        b.Stop();
        Until({&a, &observer}, [&] { return a.State() == CallState::Ready && a.Participants().size() == 1; });
        Require(b.Start("127.0.0.1", port, "rtc-test", "b", cb, {}), "Rejoin same identity failed");
        Until({&a, &b, &observer}, [&] { return b.State() == CallState::Ready && a.Participants().size() == 2; });
        PreviewCall duplicate;
        Require(duplicate.Start("127.0.0.1", port, "rtc-test", "b", {}, {}), "Duplicate TCP connect failed");
        Until({&a, &b, &observer, &duplicate}, [&] { return duplicate.State() == CallState::Offline; });
        Require(duplicate.LastStatus().find("already in use") != std::string::npos, "Duplicate identity not rejected");
        // An arbitrary registered socket cannot impersonate another participant.
        luma::client::signaling::TcpSignalingClient raw;
        std::atomic<int> registered{0}, errors{0}, pongs{0};
        using Message = luma::contracts::SignalingMessage;
        using Type = luma::contracts::SignalingMessageType;
        Require(raw.Connect("127.0.0.1", port, [&](const Message& m) {
            if (m.type == Type::RoomJoined) ++registered;
            if (m.type == Type::Error) ++errors;
            if (m.type == Type::Pong) ++pongs;
        }), "Raw signaling client failed");
        Message packet; packet.type = Type::JoinRoom; packet.room_id = "rtc-test"; packet.peer_id = "raw";
        Require(raw.Send(packet), "Raw participant registration failed");
        Until({&a, &b, &observer}, [&] { return registered > 0; });
        packet.type = Type::CallInvite; packet.peer_id = "a"; packet.target_peer_id = "b"; packet.sequence = 77;
        Require(raw.Send(packet), "Spoof probe send failed");
        Until({&a, &b, &observer}, [&] { return errors > 0; });
        Require(b.State() == CallState::Ready, "Server forwarded a spoofed sender");
        packet.peer_id = "raw"; packet.sequence = 78;
        Require(raw.Send(packet), "Valid raw invite failed");
        Until({&a, &b, &observer}, [&] { return b.State() == CallState::Incoming; });
        Require(b.Remote() == "raw" && b.Reject(), "Raw invite attribution failed");
        Require(raw.Send(packet), "Stale invite replay send failed");
        Message ping; ping.type = Type::Ping; Require(raw.Send(ping), "Barrier ping failed");
        Until({&a, &b, &observer}, [&] { return pongs > 0; });
        b.Poll(); Require(b.State() == CallState::Ready, "Stale invitation reopened a finished call");
        raw.Close();
        Until({&a, &b, &observer}, [&] { return a.Participants().size() == 2; });
        PreviewCall timeout(500ms);
        Require(timeout.Start("127.0.0.1", port, "rtc-test", "timeout", {}, {}), "Timeout caller startup failed");
        Until({&a, &b, &observer, &timeout}, [&] { return timeout.State() == CallState::Ready && !timeout.Participants().empty(); });
        Require(timeout.Call("a"), "Timeout invite failed");
        Until({&a, &b, &observer, &timeout}, [&] { return a.State() == CallState::Incoming; });
        Until({&a, &b, &observer, &timeout}, [&] { return timeout.State() == CallState::Ready && a.State() == CallState::Ready; });
        Require(timeout.LastStatus() == "Call timed out", "Timeout status missing");
        std::cout << "Stopping timeout participant\n";
        timeout.Stop();
        std::cout << "Stopping signaling server\n";
        server.Stop();
        std::cout << "Signaling server stopped\n";
        Until({&a, &b, &observer}, [&] { return !a.Active() && !b.Active() && !observer.Active(); });
        Require(a.Participants().empty() && b.Participants().empty(), "Disconnect retained participants");
        std::cout << "PASS: consent, targeting, busy, cancel, rejection, media, hangup, leave, reconnect, duplicate identity, timeout and server disconnect\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; result = 1;
    }
    CoUninitialize(); return result;
}



