// Tests real controllers and local signaling. This executable deliberately does
// not link the native WebRTC implementation or prove STUN/TURN connectivity.
#include "PreviewCall.hpp"
#include "MeetingMedia.hpp"
#include "TcpSignalingServer.hpp"
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

using Config = luma::contracts::PeerConnectionConfig;
using namespace luma::client::ui::preview;
using namespace std::chrono_literals;
namespace {
std::vector<Config> initializedConfigs, restartedConfigs;
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
bool Same(const Config& a, const Config& b) {
    return a.stun_servers == b.stun_servers && a.turn_url == b.turn_url &&
        a.turn_username == b.turn_username && a.turn_password == b.turn_password &&
        a.value == b.value && a.sequence == b.sequence;
}
class Environment {
    const std::array<const char*, 4> names_{{"LUMALIVE_STUN_SERVER", "LUMALIVE_TURN_URL",
        "LUMALIVE_TURN_USERNAME", "LUMALIVE_TURN_PASSWORD"}};
    std::array<std::optional<std::string>, 4> saved_;
public:
    Environment() {
        for (size_t i = 0; i < names_.size(); ++i)
            if (const char* value = std::getenv(names_[i])) saved_[i] = value;
        Clear();
    }
    ~Environment() {
        for (size_t i = 0; i < names_.size(); ++i) Put(names_[i], saved_[i] ? saved_[i]->c_str() : nullptr);
    }
    static int Put(const char* name, const char* value) {
#ifdef _WIN32
        return _putenv_s(name, value ? value : "");
#else
        return value ? setenv(name, value, 1) : unsetenv(name);
#endif
    }
    void Set(const char* name, const char* value) { Require(Put(name, value) == 0, "test environment setup failed"); }
    void Clear() { for (const auto* name : names_) Set(name, nullptr); }
    Config Deployment() {
        Clear();
        Set("LUMALIVE_STUN_SERVER", "stun:deployment.invalid:3478");
        Set("LUMALIVE_TURN_URL", "turn:deployment.invalid:3478?transport=tcp");
        Set("LUMALIVE_TURN_USERNAME", "configuration-test-user");
        Set("LUMALIVE_TURN_PASSWORD", "configuration-test-password");
        Config config;
        config.stun_servers = {"stun:deployment.invalid:3478"};
        config.turn_url = "turn:deployment.invalid:3478?transport=tcp";
        config.turn_username = "configuration-test-user";
        config.turn_password = "configuration-test-password";
        return config;
    }
};
template<class Pump, class Predicate>
void Until(Pump pump, Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    do {
        pump();
        if (ready()) return;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("configuration fixture signaling timed out");
}
Config Defaults() { Config result; result.stun_servers = {"stun:stun.l.google.com:19302"}; return result; }
Config Explicit() {
    Config result;
    result.stun_servers = {"stun:explicit-one.invalid:3478", "stun:explicit-two.invalid:3478"};
    result.turn_url = "turns:explicit.invalid:5349";
    result.turn_username = "explicit-test-user";
    result.turn_password = "explicit-test-password";
    result.value = "preserved metadata"; result.sequence = 42;
    return result;
}
void CheckCaptured(const Config& expected, size_t count) {
    Require(initializedConfigs.size() == count, "wrong number of initialized transports");
    for (const auto& config : initializedConfigs)
        Require(Same(config, expected), "transport configuration mismatch");
}
void LoaderRules(Environment& env) {
    env.Clear(); Require(Same(LoadPreviewRtcConfig(), Defaults()), "missing environment defaults changed");
    env.Set("LUMALIVE_STUN_SERVER", "");
    Require(Same(LoadPreviewRtcConfig(), Defaults()), "empty STUN default changed");
    env.Set("LUMALIVE_TURN_USERNAME", "ignored-user");
    env.Set("LUMALIVE_TURN_PASSWORD", "ignored-password");
    Require(Same(LoadPreviewRtcConfig(), Defaults()), "credentials leaked into config without TURN URL");
    env.Set("LUMALIVE_TURN_URL", "");
    Require(Same(LoadPreviewRtcConfig(), Defaults()), "empty TURN URL retained credentials");
    env.Clear(); env.Set("LUMALIVE_TURN_URL", "turn:relay.invalid:3478");
    auto expected = Defaults(); expected.turn_url = "turn:relay.invalid:3478";
    Require(Same(LoadPreviewRtcConfig(), expected), "missing TURN credentials changed");
    env.Set("LUMALIVE_TURN_USERNAME", "user-only"); expected.turn_username = "user-only";
    Require(Same(LoadPreviewRtcConfig(), expected), "missing TURN password changed");
    env.Set("LUMALIVE_TURN_USERNAME", nullptr); expected.turn_username.clear();
    env.Set("LUMALIVE_TURN_PASSWORD", "password-only"); expected.turn_password = "password-only";
    Require(Same(LoadPreviewRtcConfig(), expected), "missing TURN username changed");
    expected = env.Deployment();
    Require(Same(LoadPreviewRtcConfig(), expected), "configured environment changed");
    env.Set("LUMALIVE_STUN_SERVER", " stun:literal.invalid:3478 ");
    expected.stun_servers = {" stun:literal.invalid:3478 "};
    Require(Same(LoadPreviewRtcConfig(), expected), "loader unexpectedly normalized deployment values");
    std::cout << "PASS: shared environment rules\n";
}
void Calls(std::uint16_t port, Environment& env, const Config& expected, bool explicitConfig) {
    initializedConfigs.clear(); restartedConfigs.clear();
    PreviewCall a, b;
    auto start = [&](PreviewCall& call, const char* peer) {
        return explicitConfig ? call.Start("127.0.0.1", port, "config-calls", peer, {}, expected)
                              : call.Start("127.0.0.1", port, "config-calls", peer, {});
    };
    Require(start(a, "a") && start(b, "b"), "call start failed");
    auto pump = [&] { a.Poll(); b.Poll(); };
    Until(pump, [&] { return a.State() == CallState::Ready && b.State() == CallState::Ready &&
        a.Participants().size() == 1 && b.Participants().size() == 1; });
    // A session captures deployment config at Start, not later at consent.
    env.Set("LUMALIVE_TURN_URL", "turn:changed-after-start.invalid:3478");
    Require(a.Call("b"), "call invite failed");
    Until(pump, [&] { return b.State() == CallState::Incoming; });
    Require(b.Accept(), "call acceptance failed");
    Until(pump, [&] { return a.State() == CallState::Connected && b.State() == CallState::Connected; });
    CheckCaptured(expected, 2);
    Require(a.Reconnect(), "call reconnect failed");
    Until(pump, [&] { return a.State() == CallState::Connected && b.State() == CallState::Connected; });
    Require(restartedConfigs.size() == 1 && Same(restartedConfigs[0], expected), "call restart changed config");
    a.Stop(); b.Stop();
    std::cout << "PASS: call config reaches Initialize and restart\n";
}
void Meetings(std::uint16_t port, Environment& env, const Config& expected, bool explicitConfig) {
    initializedConfigs.clear(); restartedConfigs.clear();
    MeetingMedia a, b;
    auto start = [&](MeetingMedia& meeting, const char* peer, bool create) {
        return explicitConfig ? meeting.Start("127.0.0.1", port, "config-meetings", peer, create, {}, expected)
                              : meeting.Start("127.0.0.1", port, "config-meetings", peer, create, {});
    };
    Require(start(a, "a", true), "meeting host start failed");
    auto pump = [&] { a.Poll(); b.Poll(); };
    Until(pump, [&] { return a.Session().IsHost(); });
    Require(start(b, "b", false), "meeting member start failed");
    env.Set("LUMALIVE_TURN_URL", "turn:changed-after-start.invalid:3478");
    Until(pump, [&] { return a.PeerState("b") == "connected" && b.PeerState("a") == "connected"; });
    CheckCaptured(expected, 2);
    Require(a.Reconnect("b"), "meeting reconnect failed");
    Until(pump, [&] { return initializedConfigs.size() == 4 &&
        a.PeerState("b") == "connected" && b.PeerState("a") == "connected"; });
    CheckCaptured(expected, 4);
    a.Leave(); b.Leave();
    std::cout << "PASS: meeting config reaches Initialize and rebuilt transports\n";
}
}

// Substitute only the native transport at the link boundary. The production
// config type, Start overloads, consent/membership and reconnect paths are real.
namespace luma::client::webrtc {
struct NativeWebRtcPeerConnection::Impl { Config config; WebRtcCallbacks cb; bool initialized = false; };
NativeWebRtcPeerConnection::NativeWebRtcPeerConnection() : impl_(std::make_unique<Impl>()) {}
NativeWebRtcPeerConnection::~NativeWebRtcPeerConnection() = default;
std::shared_ptr<NativeWebRtcPeerConnection> NativeWebRtcPeerConnection::Create() {
    return std::shared_ptr<NativeWebRtcPeerConnection>(new NativeWebRtcPeerConnection);
}
bool NativeWebRtcPeerConnection::Initialize(const Config& config, WebRtcCallbacks cb) {
    impl_->config = config; impl_->cb = std::move(cb); impl_->initialized = true;
    initializedConfigs.push_back(config); return true;
}
bool NativeWebRtcPeerConnection::CreateOffer(bool restart) {
    if (restart) restartedConfigs.push_back(impl_->config);
    if (impl_->cb.on_local_description) impl_->cb.on_local_description("offer", "config-test-offer");
    return true;
}
bool NativeWebRtcPeerConnection::CreateAnswer() {
    if (impl_->cb.on_local_description) impl_->cb.on_local_description("answer", "config-test-answer");
    if (impl_->cb.on_connection_state) impl_->cb.on_connection_state("connected");
    return true;
}
bool NativeWebRtcPeerConnection::SetRemoteDescription(const std::string& type, const std::string&) {
    if (impl_->cb.on_remote_description_set) impl_->cb.on_remote_description_set();
    if (type == "answer" && impl_->cb.on_connection_state) impl_->cb.on_connection_state("connected");
    return true;
}
bool NativeWebRtcPeerConnection::AddRemoteIceCandidate(const std::string&, int, const std::string&) { return true; }
bool NativeWebRtcPeerConnection::AddVideoFrame(const media::pipeline::VideoFrame&) { return true; }
bool NativeWebRtcPeerConnection::AddAudioFrame(const media::pipeline::AudioFrame&) { return true; }
bool NativeWebRtcPeerConnection::GetNetworkStats(std::function<void(WebRtcNetworkStats)>) { return false; }
void NativeWebRtcPeerConnection::Close() { impl_->initialized = false; impl_->cb = {}; }
bool NativeWebRtcPeerConnection::IsInitialized() const noexcept { return impl_->initialized; }
}

int main() {
    try {
        Environment env;
        LoaderRules(env);
        luma::server::signaling::TcpSignalingServer server;
        std::uint16_t port = 19800;
        while (port < 19900 && !server.Start(port)) ++port;
        Require(port < 19900, "no available configuration test signaling port");
        for (int mode = 0; mode < 4; ++mode) {
            Config expected;
            if (mode == 0) { env.Clear(); expected = Defaults(); }
            else { expected = env.Deployment(); }
            if (mode == 2) expected = {};
            if (mode == 3) expected = Explicit();
            Calls(port, env, expected, mode >= 2);
            if (mode == 0) env.Clear(); else env.Deployment();
            Meetings(port, env, expected, mode >= 2);
        }
        server.Stop();
        std::cout << "PASS: RTC configuration plumbing; real cross-network TURN remains untested\n";
        return 0;
    } catch (const std::exception& error) {
        // Assertion messages never contain configuration or environment values.
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
