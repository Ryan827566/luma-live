#include "LocalAiService.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace luma::client::ui::preview;
using namespace std::chrono_literals;
using State = LocalAiService::State;
namespace fs = std::filesystem;

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> bool Wait(F condition, std::chrono::milliseconds timeout = 4000ms) {
 const auto deadline = std::chrono::steady_clock::now() + timeout;
 do { if (condition()) return true; std::this_thread::sleep_for(5ms); }
 while (std::chrono::steady_clock::now() < deadline);
 return condition();
}
fs::path Self() {
 wchar_t path[32768]{};
 const auto length = GetModuleFileNameW(nullptr, path, 32768);
 Require(length && length < 32768, "module path unavailable");
 return path;
}
fs::path Marker(const fs::path& script) { return fs::path(script.wstring() + L".pid"); }
DWORD Pid(const fs::path& script) { DWORD pid = 0; std::ifstream(Marker(script)) >> pid; return pid; }
struct Process {
 HANDLE handle = nullptr;
 ~Process() { if (handle) { TerminateProcess(handle, 0); WaitForSingleObject(handle, 2000); CloseHandle(handle); } }
 void Open(DWORD pid) {
  handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
  Require(handle != nullptr, "could not inspect controlled child");
 }
 bool Alive() const { return handle && WaitForSingleObject(handle, 0) == WAIT_TIMEOUT; }
};
struct Socket {
 SOCKET value = INVALID_SOCKET;
 ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
 unsigned short Listen(unsigned short port = 0) {
  value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  Require(value != INVALID_SOCKET, "socket creation failed");
  BOOL exclusive = TRUE;
  Require(!setsockopt(value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive), sizeof(exclusive)), "exclusive bind failed");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  Require(!bind(value, reinterpret_cast<sockaddr*>(&address), sizeof(address)), "test bind failed");
  int size = sizeof(address);
  Require(!getsockname(value, reinterpret_cast<sockaddr*>(&address), &size), "socket port unavailable");
  Require(!listen(value, 4), "test listen failed");
  return ntohs(address.sin_port);
 }
};
LocalAiService::Options Options(const fs::path& script) {
 LocalAiService::Options options;
 options.executable = Self(); options.script = script;
 options.readinessTimeout = 2500ms; options.pollInterval = 20ms;
 return options;
}
fs::path Script(const fs::path& root, const std::wstring& name, const char* mode) {
 const auto script = root / name;
 std::ofstream(script) << mode;
 return script;
}
void SpawnExternal(const fs::path& script, Process& child) {
 const auto executable = Self();
 auto command = L"\"" + executable.wstring() + L"\" -I -B \"" + script.wstring() + L"\"";
 STARTUPINFOW startup{}; startup.cb = sizeof(startup);
 PROCESS_INFORMATION process{};
 Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
  CREATE_NO_WINDOW, nullptr, script.parent_path().c_str(), &startup, &process) != 0, "external fixture launch failed");
 CloseHandle(process.hThread); child.handle = process.hProcess;
}
int ChildMain(const fs::path& script) {
 std::string mode; std::ifstream(script) >> mode;
 BOOL job = FALSE;
 IsProcessInJob(GetCurrentProcess(), nullptr, &job);
 { std::ofstream marker(Marker(script)); marker << GetCurrentProcessId() << ' ' << (GetConsoleWindow() == nullptr) << ' ' << bool(job); }
 if (mode == "exit") return 17;
 if (mode == "supervise") {
  const auto nested = Script(script.parent_path(), L"nested gateway.py", "health");
  LocalAiService service(Options(nested));
  Require(service.Start() && Wait([&] { return service.Status() == State::Running; }), "supervisor service did not start");
  Sleep(60000); return 0;
 }
 if (mode == "health") {
  WSADATA data{}; Require(!WSAStartup(MAKEWORD(2, 2), &data), "child winsock failed");
  INTERNET_PORT port = 0; Require(AiServiceHealth::ReadGatewayPort(port), "child port invalid");
  Socket server; server.Listen(port);
  for (;;) {
   const auto connection = accept(server.value, nullptr, nullptr);
   if (connection == INVALID_SOCKET) return 18;
   char request[2048]{};
   recv(connection, request, sizeof(request), 0);
   constexpr char response[] = "HTTP/1.1 200 OK\r\nX-Luma-AI-Protocol: 1\r\nX-Luma-AI-Health: configuration-required\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
   send(connection, response, sizeof(response) - 1, 0);
   closesocket(connection);
  }
 }
 Sleep(60000); return 0;
}
void LaunchAndStop(const fs::path& root) {
 const auto script = Script(root, L"space & unicode \u7f51\u5173.py", "health");
 LocalAiService service(Options(script));
 const auto started = std::chrono::steady_clock::now();
 Require(service.Start(), "start rejected");
 Require(std::chrono::steady_clock::now() - started < 500ms, "start blocked UI");
 Require(!service.Start(), "duplicate start accepted");
 Require(Wait([&] { return service.Status() == State::Running && !service.Busy(); }), "controlled gateway did not become ready");
 Require(service.Owned(), "ready child is not owned");
 DWORD pid = 0; bool hidden = false, job = false;
 std::ifstream(Marker(script)) >> pid >> hidden >> job;
 Require(pid && hidden && job, "child was not hidden and job-contained, or quoting failed");
 Process child; child.Open(pid);
 service.Stop();
 Require(service.Status() == State::Stopped && !service.Owned(), "stop retained ownership");
 Require(Wait([&] { return !child.Alive(); }), "stop did not terminate owned child");
}
void CancellationAndTimeout(const fs::path& root) {
 const auto script = Script(root, L"cancel.py", "wait");
 auto options = Options(script);
 auto calls = std::make_shared<std::atomic<int>>(0);
 auto entered = std::make_shared<std::atomic<bool>>(false);
 options.probe = [=](const std::atomic<bool>& canceled, INTERNET_PORT) {
  if (++*calls > 1) { entered->store(true); Wait([&] { return canceled.load(); }, 5000ms); }
  return AiServiceHealth::State::Unavailable;
 };
 LocalAiService service(options);
 Require(service.Start() && Wait([&] { return entered->load() && Pid(script); }), "cancel fixture did not launch");
 Process child; child.Open(Pid(script));
 const auto started = std::chrono::steady_clock::now();
 service.Stop();
 Require(std::chrono::steady_clock::now() - started < 500ms, "stop waited for health request");
 Require(Wait([&] { return !service.Busy() && !child.Alive(); }), "cancellation did not drain and clean up");
 Require(service.Status() == State::Stopped, "late health result overwrote cancellation");

 options = Options(Script(root, L"timeout.py", "wait"));
 options.readinessTimeout = 500ms;
 options.probe = [](const std::atomic<bool>&, INTERNET_PORT) { return AiServiceHealth::State::Unavailable; };
 LocalAiService timeout(options);
 Require(timeout.Start() && Wait([&] { return Pid(options.script) != 0; }), "timeout child did not launch");
 Process timedChild; timedChild.Open(Pid(options.script));
 Require(Wait([&] { return !timeout.Busy(); }), "readiness timeout was unbounded");
 Require(timeout.Status() == State::Failed && !timeout.Owned(), "readiness timeout state wrong");
 Require(Wait([&] { return !timedChild.Alive(); }), "timed out child leaked");
}
void ExternalAndMissing(const fs::path& root) {
 const auto script = Script(root, L"external.py", "health");
 Process external; SpawnExternal(script, external);
 std::atomic<bool> canceled{false};
 Require(Wait([&] { return AiServiceHealth::Probe(canceled) == AiServiceHealth::State::ConfigurationRequired; }), "external health fixture unavailable");
 auto options = Options(root / L"missing.py");
 LocalAiService service(options);
 Require(service.Start() && Wait([&] { return !service.Busy(); }), "external detection did not finish");
 Require(service.Status() == State::ExternalRunning && !service.Owned(), "external gateway was adopted");
 service.Stop();
 Require(external.Alive(), "stop killed an external gateway");
 Require(service.Start() && Wait([&] { return !service.Busy(); }), "external recheck failed");
 TerminateProcess(external.handle, 0); WaitForSingleObject(external.handle, 2000);
 Require(service.Start() && Wait([&] { return !service.Busy(); }), "exited external gateway could not be retried");
 Require(service.Status() == State::MissingFiles, "missing package did not report MissingFiles");
}
void ExitsPortAndDestruction(const fs::path& root) {
 auto options = Options(Script(root, L"exit.py", "exit"));
 options.probe = [](const std::atomic<bool>&, INTERNET_PORT) { return AiServiceHealth::State::Unavailable; };
 LocalAiService exited(options);
 Require(exited.Start() && Wait([&] { return !exited.Busy(); }), "exit test stuck");
 Require(exited.Status() == State::Exited && !exited.Owned(), "child exit not detected");
 {
  INTERNET_PORT port = 0; Require(AiServiceHealth::ReadGatewayPort(port), "test port invalid");
  Socket occupied; occupied.Listen(port);
  options.script = Script(root, L"busy.py", "wait");
  LocalAiService busy(options);
  Require(busy.Start() && Wait([&] { return !busy.Busy(); }), "occupied port test stuck");
  Require(busy.Status() == State::PortBusy && !busy.Owned() && !Pid(options.script), "occupied port launched a child");
 }
 Process child;
 {
  auto service = std::make_unique<LocalAiService>(Options(Script(root, L"destructor.py", "health")));
  Require(service->Start() && Wait([&] { return service->Status() == State::Running; }), "destructor fixture did not start");
  child.Open(Pid(root / L"destructor.py"));
 }
 Require(Wait([&] { return !child.Alive(); }), "destructor leaked child");
 // The OS must also clean up the job when the owning host exits abruptly.
 Process supervisor;
 SpawnExternal(Script(root, L"supervise.py", "supervise"), supervisor);
 const auto nested = root / L"nested gateway.py";
 Require(Wait([&] { return Pid(nested) != 0; }), "nested child did not launch");
 Process nestedChild; nestedChild.Open(Pid(nested));
 TerminateProcess(supervisor.handle, 0);
 Require(Wait([&] { return !nestedChild.Alive(); }), "host death leaked job child");
}
}

int wmain(int argc, wchar_t** argv) {
 try {
  if (argc == 4 && std::wstring(argv[1]) == L"-I" && std::wstring(argv[2]) == L"-B") return ChildMain(argv[3]);
  Require(argc == 1, "unexpected test arguments");
  WSADATA data{}; Require(!WSAStartup(MAKEWORD(2, 2), &data), "winsock unavailable");
  unsigned short port = 0; { Socket reservation; port = reservation.Listen(); }
  const wchar_t* name = L"LUMALIVE_AI_GATEWAY_PORT";
  const auto length = GetEnvironmentVariableW(name, nullptr, 0);
  std::wstring previous(length, L'\0');
  if (length) previous.resize(GetEnvironmentVariableW(name, previous.data(), length));
  struct Restore { const wchar_t* name; std::wstring previous; bool existed;
   ~Restore() { SetEnvironmentVariableW(name, existed ? previous.c_str() : nullptr); WSACleanup(); }
  } restore{name, previous, length != 0};
  Require(SetEnvironmentVariableW(name, std::to_wstring(port).c_str()) != 0, "test port setup failed");
  const auto root = fs::temp_directory_path() / (L"Luma AI lifecycle \u6d4b\u8bd5 " + std::to_wstring(GetCurrentProcessId()));
  fs::create_directories(root);
  struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
  Require(&LocalAiService::Instance() == &LocalAiService::Instance(), "singleton is not shared");
  Require(LocalAiService::Instance().Status() == State::Stopped, "singleton constructor launched a process");
  LaunchAndStop(root);
  CancellationAndTimeout(root);
  ExternalAndMissing(root);
  ExitsPortAndDestruction(root);
  std::cout << "PASS: hidden job-owned gateway, quoting, cancellation, readiness timeout, external ownership, missing package, occupied port, exit and host-death cleanup\n";
  return 0;
 } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
