#pragma once
#include <winsock2.h>
#include "AiServiceHealth.hpp"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <vector>

namespace luma::client::ui::preview {
// Process-wide gateway ownership is independent of any call, panel or caption
// session. Starting this service only checks /health; it never calls a provider.
class LocalAiService {
public:
 enum class State { Stopped, Starting, Running, ExternalRunning, MissingFiles, Failed, Exited, PortBusy };
 using Probe = std::function<AiServiceHealth::State(const std::atomic<bool>&, INTERNET_PORT)>;
 struct Options {
  // Empty paths resolve exclusively beside this executable, never through PATH.
  // Explicit absolute paths and probes are a seam for controlled process tests.
  std::filesystem::path executable, script;
  Probe probe = [](const std::atomic<bool>& canceled, INTERNET_PORT port) {
   return AiServiceHealth::Probe(canceled, port);
  };
  std::chrono::milliseconds readinessTimeout{8000}, pollInterval{100};
 };

 static LocalAiService& Instance() { static LocalAiService service; return service; }
 LocalAiService() : LocalAiService(Options{}) {}
 explicit LocalAiService(Options options) : shared_(std::make_shared<Shared>(std::move(options))) {}
 ~LocalAiService() {
  std::lock_guard lock(shared_->mutex);
  shared_->destroyed = true;
  StopLocked(*shared_);
 }
 LocalAiService(const LocalAiService&) = delete;
 LocalAiService& operator=(const LocalAiService&) = delete;

 // Returns immediately; concurrent starts and starts while cancellation drains
 // are rejected. A failed/exited service can be started again.
 bool Start() {
  const auto shared = shared_;
  std::lock_guard lock(shared->mutex);
  RefreshExitLocked(*shared);
  if (shared->destroyed || shared->inFlight || shared->owned) return false;
  try {
   auto canceled = std::make_shared<std::atomic<bool>>(false);
   shared->canceled = canceled;
   shared->inFlight = true;
   shared->status = State::Starting;
   std::thread([shared, canceled] {
    try { Run(shared, canceled); }
    catch (...) { Finish(shared, canceled, State::Failed); }
    std::lock_guard lock(shared->mutex);
    shared->inFlight = false;
    shared->canceled.reset();
   }).detach();
   return true;
  } catch (...) {
   shared->inFlight = false;
   shared->canceled.reset();
   shared->status = State::Failed;
   return false;
  }
 }
 void Stop() {
  std::lock_guard lock(shared_->mutex);
  StopLocked(*shared_);
 }
 State Status() const {
  std::lock_guard lock(shared_->mutex);
  RefreshExitLocked(*shared_);
  return shared_->status;
 }
 bool Busy() const {
  std::lock_guard lock(shared_->mutex);
  return shared_->inFlight;
 }
 bool Owned() const {
  std::lock_guard lock(shared_->mutex);
  RefreshExitLocked(*shared_);
  return bool(shared_->owned);
 }

private:
 struct Child {
  HANDLE process = nullptr, job = nullptr;
  bool assigned = false;
  ~Child() { Stop(); if (process) CloseHandle(process); }
  void Stop() {
   // An unassigned child is still suspended and belongs to this launch only.
   if (process && !assigned) TerminateProcess(process, ERROR_CANCELLED);
   if (job) { CloseHandle(job); job = nullptr; }
  }
 };
 struct Shared {
  explicit Shared(Options value) : options(std::move(value)) {}
  Options options;
  std::mutex mutex;
  std::condition_variable wake;
  std::shared_ptr<std::atomic<bool>> canceled;
  std::shared_ptr<Child> owned;
  State status = State::Stopped;
  bool inFlight = false, destroyed = false;
 };
 static void StopLocked(Shared& shared) {
  if (shared.canceled) shared.canceled->store(true);
  if (shared.owned) { shared.owned->Stop(); shared.owned.reset(); }
  shared.status = State::Stopped;
  shared.wake.notify_all();
 }
 static void RefreshExitLocked(Shared& shared) {
  if (shared.owned && WaitForSingleObject(shared.owned->process, 0) == WAIT_OBJECT_0) {
   // Closing the job also removes descendants of an unexpectedly exited host.
   shared.owned->Stop();
   shared.owned.reset();
   shared.status = State::Exited;
  }
 }
 static bool Healthy(AiServiceHealth::State value) {
  return value == AiServiceHealth::State::Ready || value == AiServiceHealth::State::ConfigurationRequired;
 }
 static void Finish(const std::shared_ptr<Shared>& shared,
                    const std::shared_ptr<std::atomic<bool>>& canceled, State state) {
  std::lock_guard lock(shared->mutex);
  if (canceled->load() || shared->destroyed) return;
  if (state == State::Running && (!shared->owned ||
      WaitForSingleObject(shared->owned->process, 0) == WAIT_OBJECT_0)) state = State::Exited;
  if (state != State::Running && shared->owned) {
   shared->owned->Stop();
   shared->owned.reset();
  }
  shared->status = state;
 }
 static std::filesystem::path ModuleDirectory() {
  std::vector<wchar_t> path(32768);
  const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!length || length >= path.size()) return {};
  return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
 }
 static std::wstring Quote(const std::wstring& argument) {
  // CommandLineToArgvW/CRT rules: backslashes preceding quotes and the closing
  // quote are doubled. lpApplicationName separately pins the executable.
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (const auto c : argument) {
   if (c == L'\\') { ++slashes; continue; }
   result.append(c == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
   result += c;
   slashes = 0;
  }
  result.append(slashes * 2, L'\\');
  result += L'\"';
  return result;
 }
 enum class Port { Free, Busy, Failed };
 static Port CheckPort(INTERNET_PORT port) {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data)) return Port::Failed;
  const auto socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socketHandle == INVALID_SOCKET) { WSACleanup(); return Port::Failed; }
  BOOL exclusive = TRUE;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Port result = Port::Failed;
  if (setsockopt(socketHandle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == 0) {
   if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
    result = Port::Free;
   else {
    const auto error = WSAGetLastError();
    if (error == WSAEADDRINUSE || error == WSAEACCES) result = Port::Busy;
   }
  }
  closesocket(socketHandle);
  WSACleanup();
  return result;
 }
 static void Run(const std::shared_ptr<Shared>& shared,
                 const std::shared_ptr<std::atomic<bool>>& canceled) {
  INTERNET_PORT port = 19740;
  if (!AiServiceHealth::ReadGatewayPort(port)) { Finish(shared, canceled, State::Failed); return; }
  if (canceled->load()) return;
  const auto existing = shared->options.probe(*canceled, port);
  if (canceled->load()) return;
  if (Healthy(existing)) { Finish(shared, canceled, State::ExternalRunning); return; }
  if (existing != AiServiceHealth::State::Unavailable) { Finish(shared, canceled, State::PortBusy); return; }

  auto executable = shared->options.executable, script = shared->options.script;
  if (executable.empty() && script.empty()) {
   const auto directory = ModuleDirectory();
   if (!directory.empty()) {
    executable = directory / L"ai-service" / L"runtime" / L"python.exe";
    script = directory / L"ai-service" / L"gateway.py";
   }
  }
  std::error_code error;
  if (!executable.is_absolute() || !script.is_absolute() ||
      !std::filesystem::is_regular_file(executable, error) ||
      !std::filesystem::is_regular_file(script, error)) {
   Finish(shared, canceled, State::MissingFiles); return;
  }
  const auto availability = CheckPort(port);
  if (availability != Port::Free) {
   Finish(shared, canceled, availability == Port::Busy ? State::PortBusy : State::Failed); return;
  }
  if (canceled->load()) return;
  auto child = std::make_shared<Child>();
  child->job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!child->job || !SetInformationJobObject(child->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
   Finish(shared, canceled, State::Failed); return;
  }
  auto command = Quote(executable.wstring()) + L" -I -B " + Quote(script.wstring());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  const auto directory = script.parent_path().wstring();
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &process)) {
   Finish(shared, canceled, State::Failed); return;
  }
  child->process = process.hProcess;
  if (!AssignProcessToJobObject(child->job, child->process)) {
   // Never resume a process outside our kill-on-close job.
   child->Stop();
   CloseHandle(process.hThread);
   Finish(shared, canceled, State::Failed); return;
  }
  child->assigned = true;
  {
   std::lock_guard lock(shared->mutex);
   if (canceled->load() || shared->destroyed) {
    child->Stop(); CloseHandle(process.hThread); return;
   }
   shared->owned = child;
   const auto resumed = ResumeThread(process.hThread);
   CloseHandle(process.hThread);
   if (resumed == DWORD(-1)) {
    child->Stop(); shared->owned.reset(); shared->status = State::Failed; return;
   }
  }
  const auto deadline = std::chrono::steady_clock::now() + shared->options.readinessTimeout;
  while (!canceled->load()) {
   if (WaitForSingleObject(child->process, 0) == WAIT_OBJECT_0) { Finish(shared, canceled, State::Exited); return; }
   const auto health = shared->options.probe(*canceled, port);
   if (canceled->load()) return;
   if (WaitForSingleObject(child->process, 0) == WAIT_OBJECT_0) { Finish(shared, canceled, State::Exited); return; }
   if (Healthy(health)) { Finish(shared, canceled, State::Running); return; }
   if (health != AiServiceHealth::State::Unavailable) { Finish(shared, canceled, State::PortBusy); return; }
   if (std::chrono::steady_clock::now() >= deadline) { Finish(shared, canceled, State::Failed); return; }
   std::unique_lock lock(shared->mutex);
   shared->wake.wait_for(lock, shared->options.pollInterval, [&] { return canceled->load(); });
  }
 }
 std::shared_ptr<Shared> shared_;
};
}
