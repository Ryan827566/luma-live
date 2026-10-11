#pragma once
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace luma::client::ui::preview {
// Checks local gateway configuration without calling a model/provider.
// Protocol headers identify a response format; they are not authentication.
class AiServiceHealth {
public:
 enum class State { Idle, Checking, Unavailable, ConfigurationRequired, Ready, InvalidResponse };
 using Transport = std::function<State(const std::atomic<bool>& canceled)>;

 explicit AiServiceHealth(Transport transport = Http)
  : shared_(std::make_shared<Shared>(std::move(transport))) {}
 ~AiServiceHealth() {
  std::lock_guard lock(shared_->mutex);
  shared_->stopped = true;
  InvalidateLocked(*shared_);
 }
 AiServiceHealth(const AiServiceHealth&) = delete;
 AiServiceHealth& operator=(const AiServiceHealth&) = delete;

 // Rejects another request until the current one drains, even after Invalidate.
 // Workers own shared state, never this or an HWND; destruction does not wait.
 bool Check() {
  auto shared = shared_;
  std::lock_guard lock(shared->mutex);
  if (shared->stopped || shared->inFlight) return false;
  try {
   auto canceled = std::make_shared<std::atomic<bool>>(false);
   const auto generation = ++shared->generation;
   shared->canceled = canceled;
   shared->inFlight = true;
   shared->status = State::Checking;
   std::thread([shared, canceled, generation] {
    State result = State::Unavailable;
    try {
     if (!canceled->load()) result = shared->transport(*canceled);
    } catch (...) { result = State::Unavailable; }
    if (result == State::Idle || result == State::Checking) result = State::InvalidResponse;
    std::lock_guard lock(shared->mutex);
    if (!shared->stopped && shared->generation == generation && !canceled->load())
     shared->status = result;
    shared->inFlight = false;
    shared->canceled.reset();
   }).detach();
   return true;
  } catch (...) {
   shared->inFlight = false;
   shared->canceled.reset();
   shared->status = State::Unavailable;
   return false;
  }
 }
 State Status() const {
  std::lock_guard lock(shared_->mutex);
  return shared_->status;
 }
 bool Busy() const {
  std::lock_guard lock(shared_->mutex);
  return shared_->inFlight;
 }
 void Invalidate() {
  std::lock_guard lock(shared_->mutex);
  InvalidateLocked(*shared_);
 }

private:
 struct Shared {
  explicit Shared(Transport value) : transport(std::move(value)) {}
  Transport transport;
  std::mutex mutex;
  std::shared_ptr<std::atomic<bool>> canceled;
  std::uint64_t generation = 0;
  State status = State::Idle;
  bool inFlight = false, stopped = false;
 };
 static void InvalidateLocked(Shared& shared) {
  ++shared.generation;
  shared.status = State::Idle;
  if (shared.canceled) shared.canceled->store(true);
 }
 struct Handle {
  HINTERNET value = nullptr;
  ~Handle() { if (value) WinHttpCloseHandle(value); }
  operator HINTERNET() const { return value; }
 };
 static bool GatewayPort(INTERNET_PORT& port) {
  wchar_t configured[16]{};
  SetLastError(ERROR_SUCCESS);
  const auto length = GetEnvironmentVariableW(L"LUMALIVE_AI_GATEWAY_PORT", configured, 16);
  if (!length) return GetLastError() == ERROR_ENVVAR_NOT_FOUND;
  if (length >= 16) return false;
  unsigned value = 0;
  for (DWORD i = 0; i < length; ++i) {
   if (configured[i] < L'0' || configured[i] > L'9') return false;
   value = value * 10 + unsigned(configured[i] - L'0');
   if (value > 65535) return false;
  }
  if (!value) return false;
  port = static_cast<INTERNET_PORT>(value);
  return true;
 }
 static bool SingleHeader(HINTERNET request, const wchar_t* name, std::wstring& result) {
  wchar_t value[64]{};
  DWORD size = sizeof(value), index = 0;
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value, &size, &index)) return false;
  result.assign(value, size / sizeof(wchar_t));
  // A duplicate (even one too large for this buffer) makes the response ambiguous.
  size = sizeof(value);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value, &size, &index)) return false;
  return GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND;
 }
 static State HttpFailure() {
  const auto error = GetLastError();
  return error == ERROR_WINHTTP_INVALID_SERVER_RESPONSE || error == ERROR_WINHTTP_HEADER_SIZE_OVERFLOW
   ? State::InvalidResponse : State::Unavailable;
 }
 static State Http(const std::atomic<bool>& canceled) {
  INTERNET_PORT port = 19740;
  if (!GatewayPort(port)) return State::InvalidResponse;
  if (canceled.load()) return State::Unavailable;
  Handle session{WinHttpOpen(L"LumaLive/1", WINHTTP_ACCESS_TYPE_NO_PROXY,
   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.value || !WinHttpSetTimeouts(session, 1000, 1000, 1000, 1000)) return State::Unavailable;
  // Bound trickled response headers in addition to individual socket operations.
  DWORD retries = 0, responseTimeout = 2000, headerLimit = 16384;
  if (!WinHttpSetOption(session, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)) ||
      !WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &responseTimeout, sizeof(responseTimeout)) ||
      !WinHttpSetOption(session, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headerLimit, sizeof(headerLimit)))
   return State::Unavailable;
  Handle connection{WinHttpConnect(session, L"127.0.0.1", port, 0)};
  Handle request{connection.value ? WinHttpOpenRequest(connection, L"GET", L"/health", nullptr,
   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr};
  if (!request.value) return State::Unavailable;
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  DWORD disabled = WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES;
  if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
      !WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
   return State::Unavailable;
  // Do not close synchronous WinHTTP handles from another thread. Cancellation
  // fences the result immediately and skips later stages; these calls are bounded.
  if (canceled.load()) return State::Unavailable;
  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
      WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return HttpFailure();
  if (canceled.load()) return State::Unavailable;
  if (!WinHttpReceiveResponse(request, nullptr)) return HttpFailure();
  if (canceled.load()) return State::Unavailable;
  DWORD code = 0, size = sizeof(code);
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX) || code != 200)
   return State::InvalidResponse;
  std::wstring protocol, health;
  if (!SingleHeader(request, L"X-Luma-AI-Protocol", protocol) || protocol != L"1" ||
      !SingleHeader(request, L"X-Luma-AI-Health", health)) return State::InvalidResponse;
  if (health == L"ready") return State::Ready;
  if (health == L"configuration-required") return State::ConfigurationRequired;
  return State::InvalidResponse;
 }
 std::shared_ptr<Shared> shared_;
};
}
