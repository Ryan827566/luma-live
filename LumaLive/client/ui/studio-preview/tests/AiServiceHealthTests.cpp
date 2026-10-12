#include "AiServiceHealth.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace luma::client::ui::preview;
using namespace std::chrono_literals;
using State = AiServiceHealth::State;

namespace {
void Require(bool value, const char* message) {
 if (!value) throw std::runtime_error(message);
}
template<class F> bool Wait(F condition, std::chrono::milliseconds timeout = 2000ms) {
 const auto deadline = std::chrono::steady_clock::now() + timeout;
 do {
  if (condition()) return true;
  std::this_thread::sleep_for(1ms);
 } while (std::chrono::steady_clock::now() < deadline);
 return condition();
}
const char* Name(State state) {
 switch (state) {
  case State::Idle: return "idle";
  case State::Checking: return "checking";
  case State::Unavailable: return "unavailable";
  case State::ConfigurationRequired: return "configuration-required";
  case State::Ready: return "ready";
  case State::InvalidResponse: return "invalid-response";
 }
 return "unknown";
}
int Probe(const std::string& expected) {
 Require(expected == "ready" || expected == "configuration-required" ||
  expected == "unavailable" || expected == "invalid-response", "unknown expected probe state");
 AiServiceHealth health;
 Require(health.Check(), "probe request was rejected");
 Require(Wait([&] { return health.Status() != State::Checking; }, 7000ms), "probe did not finish within bound");
 std::cout << "health=" << Name(health.Status()) << '\n';
 Require(expected == Name(health.Status()), "unexpected health probe state");
 return 0;
}
void SingleFlightAndStaleResult() {
 auto entered = std::make_shared<std::atomic<bool>>(false);
 auto release = std::make_shared<std::atomic<bool>>(false);
 auto calls = std::make_shared<std::atomic<int>>(0);
 auto sawCancellation = std::make_shared<std::atomic<bool>>(false);
 AiServiceHealth health([=](const std::atomic<bool>& canceled) {
  if (++*calls == 1) {
   entered->store(true);
   Require(Wait([&] { return release->load(); }, 5000ms), "test transport gate timed out");
   sawCancellation->store(canceled.load());
   return State::Ready; // Deliberately returns stale success after invalidation.
  }
  return State::ConfigurationRequired;
 });
 Require(health.Status() == State::Idle && calls->load() == 0, "construction performed a request");
 const auto started = std::chrono::steady_clock::now();
 Require(health.Check(), "first request rejected");
 Require(std::chrono::steady_clock::now() - started < 500ms, "Check blocked on transport");
 Require(Wait([&] { return entered->load(); }), "request did not start");
 Require(health.Status() == State::Checking, "missing checking state");
 std::atomic<int> accepted{0};
 std::vector<std::thread> contenders;
 for (int i = 0; i < 8; ++i) contenders.emplace_back([&] { if (health.Check()) ++accepted; });
 for (auto& thread : contenders) thread.join();
 Require(accepted == 0 && calls->load() == 1, "overlapping requests were accepted");
 health.Invalidate();
 health.Invalidate();
 Require(health.Status() == State::Idle, "invalidation retained previous state");
 Require(!health.Check(), "invalidating allowed overlapping network requests");
 release->store(true);
 Require(Wait([&] { return sawCancellation->load(); }), "transport did not observe cancellation");
 bool stalePublished = false;
 Require(Wait([&] {
  stalePublished = stalePublished || health.Status() != State::Idle;
  return health.Check();
 }), "fresh check rejected after stale request drained");
 Require(!stalePublished, "stale response was published after invalidation");
 Require(Wait([&] { return health.Status() == State::ConfigurationRequired; }), "fresh check did not replace stale response");
 Require(calls->load() == 2, "unexpected number of transport calls");
 health.Invalidate();
 Require(health.Status() == State::Idle, "completed result survived invalidation");
 Require(health.Check() && Wait([&] { return health.Status() == State::ConfigurationRequired; }), "later recheck failed");
}
void ResultsAndExceptions() {
 for (State result : {State::Unavailable, State::ConfigurationRequired, State::Ready, State::InvalidResponse}) {
  AiServiceHealth health([=](const std::atomic<bool>&) { return result; });
  Require(health.Check() && Wait([&] { return health.Status() == result; }), "terminal result was not published");
  Require(health.Check() && Wait([&] { return health.Status() == result; }), "completed request prevented a recheck");
 }
 auto calls = std::make_shared<std::atomic<int>>(0);
 AiServiceHealth throwing([calls](const std::atomic<bool>&) {
  if (++*calls == 1) throw std::runtime_error("test transport failure");
  return State::Ready;
 });
 Require(throwing.Check() && Wait([&] { return throwing.Status() == State::Unavailable; }), "exception escaped or left request checking");
 Require(throwing.Check() && Wait([&] { return throwing.Status() == State::Ready; }), "exception prevented later recovery");
 for (State invalid : {State::Idle, State::Checking}) {
  AiServiceHealth health([=](const std::atomic<bool>&) { return invalid; });
  Require(health.Check() && Wait([&] { return health.Status() == State::InvalidResponse; }), "invalid transport result left request pending");
 }
}
void DestructionWhileInFlight() {
 auto entered = std::make_shared<std::atomic<bool>>(false);
 auto release = std::make_shared<std::atomic<bool>>(false);
 auto canceledAfterClose = std::make_shared<std::atomic<bool>>(false);
 auto lifetime = std::make_shared<int>(1);
 std::weak_ptr<int> retained = lifetime;
 auto health = std::make_unique<AiServiceHealth>([=](const std::atomic<bool>& canceled) {
  (void)lifetime;
  entered->store(true);
  Wait([&] { return release->load(); }, 5000ms);
  canceledAfterClose->store(canceled.load());
  return State::Ready;
 });
 lifetime.reset();
 Require(health->Check() && Wait([&] { return entered->load(); }), "destruction test did not enter transport");
 const auto started = std::chrono::steady_clock::now();
 health.reset();
 Require(std::chrono::steady_clock::now() - started < 500ms, "destructor blocked on active transport");
 Require(!retained.expired(), "active worker lost its transport lifetime");
 release->store(true);
 Require(Wait([&] { return retained.expired(); }), "finished worker retained resources after destruction");
 Require(canceledAfterClose->load(), "destruction did not cancel active request");
}
void InvalidPorts() {
 constexpr auto name = L"LUMALIVE_AI_GATEWAY_PORT";
 const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
 std::wstring previous(length, L'\0');
 if (length) {
  const auto copied = GetEnvironmentVariableW(name, previous.data(), length);
  previous.resize(copied);
 }
 struct Restore {
  const wchar_t* name;
  const std::wstring& previous;
  bool existed;
  ~Restore() { SetEnvironmentVariableW(name, existed ? previous.c_str() : nullptr); }
 } restore{name, previous, length != 0};
 for (const wchar_t* port : {L"0", L"65536", L"-1", L"+19740", L"19740x", L" 19740", L"99999999999999999999"}) {
  Require(SetEnvironmentVariableW(name, port) != 0, "could not set test port");
  AiServiceHealth health;
  Require(health.Check() && Wait([&] { return health.Status() == State::InvalidResponse; }), "invalid gateway port was not rejected");
 }
}
}

int main(int argc, char** argv) {
 try {
  if (argc == 3 && std::string(argv[1]) == "--probe") return Probe(argv[2]);
  Require(argc == 1, "usage: luma_ai_service_health_tests [--probe expected-state]");
  SingleFlightAndStaleResult();
  ResultsAndExceptions();
  DestructionWhileInFlight();
  InvalidPorts();
  std::cout << "PASS: single-flight health checks, stale-result fencing, rechecks, exceptions, safe destruction and invalid ports\n";
  return 0;
 } catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
 }
}
