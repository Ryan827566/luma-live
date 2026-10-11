#include "SessionArchive.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Archive = luma::client::ui::preview::SessionArchive;
using luma::client::ui::preview::UiUtf8;
namespace fs = std::filesystem;
static void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int wmain(int argc, wchar_t** argv) {
 SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE", L"en-US");
 std::wstring error;
 if (argc == 3 && std::wstring(argv[1]) == L"--write-fixture") {
  fs::path root(argv[2]);
  if (!Archive::Save("call", UiUtf8(L"# Synthetic call record\n\nThis is harmless UI test data.\n\n## Transcript\nTest caller: \u4f60\u597d\uff0c\u8fd9\u662f\u6d4b\u8bd5\u8bb0\u5f55\u3002\n"), error, root) ||
      !Archive::Save("meeting", "# Synthetic meeting record\n\nThis is harmless UI test data.\n\n## Summary\nParticipants agreed to test the archive.\n", error, root)) { std::wcerr << error << L'\n'; return 1; }
  std::cout << "PASS: synthetic call and meeting fixtures saved\n"; return 0;
 }
 if (argc != 1) { std::cerr << "Usage: SessionArchiveTests [--write-fixture ROOT]\n"; return 2; }
 GUID guid{}; wchar_t guidText[40]{};
 if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, guidText, 40)) return 1;
 fs::path root = fs::temp_directory_path() / (std::wstring(L"LumaLiveSessionArchiveTests-") + guidText);
 if (!fs::create_directory(root)) return 1;
 struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
 try {
  Require(Archive::List("call", error, root).empty() && error.empty() && !fs::exists(root / "call"), "empty listing must not create directories");
  const std::string unicode = UiUtf8(L"# \u4f1a\u8bae\u8bb0\u5f55 \U0001f30f\n\n\u4f60\u597d\uff0c\u4e16\u754c\u3002\n");
  Require(Archive::Save("call", unicode, error, root) && error.empty(), "unicode save");
  auto first = Archive::List("call", error, root);
  Require(first.size() == 1 && !first[0].title.empty() && error.empty(), "first record listing");
  Require(Archive::Save("call", unicode, error, root), "second save");
  auto records = Archive::List("call", error, root);
  Require(records.size() == 2 && records[0].id != records[1].id && records[0].id > records[1].id, "unique newest-first ids");
  std::string loaded;
  Require(Archive::Load("call", first[0].id, loaded, error, root) && loaded == unicode, "unicode roundtrip");
  Require(Archive::List("meeting", error, root).empty(), "context listing isolation");
  Require(!Archive::Load("meeting", first[0].id, loaded, error, root) && loaded.empty() && !error.empty(), "context load isolation");
  Require(Archive::Save("meeting", "# Meeting\n", error, root) && Archive::List("meeting", error, root).size() == 1, "meeting save");
  for (const auto& id : {"../record.md", "..\\record.md", "C:\\record.md", "record.md", ""})
   Require(!Archive::Load("call", id, loaded, error, root) && !error.empty(), "invalid id rejected");
  Require(!Archive::Save("../call", unicode, error, root) && !error.empty(), "invalid context rejected");
  Require(Archive::List("unknown", error, root).empty() && !error.empty(), "invalid list context rejected");
  Require(!Archive::Save("call", "", error, root) && !error.empty(), "empty save rejected");
  Require(!Archive::Save("call", std::string(Archive::MaxBytes + 1, 'x'), error, root), "oversize save rejected");
  Require(!Archive::Save("call", std::string("\xff", 1), error, root), "invalid UTF-8 rejected");
  const std::string boundary(Archive::MaxBytes, 'x');
  Require(Archive::Save("meeting", boundary, error, root), "exact size limit accepted");
  for (const auto& entry : Archive::List("meeting", error, root)) Require(Archive::Load("meeting", entry.id, loaded, error, root), "saved files readable");
  { std::ofstream file(root / "call" / first[0].id, std::ios::binary | std::ios::trunc); file << std::string(Archive::MaxBytes + 1, 'x'); }
  Require(!Archive::Load("call", first[0].id, loaded, error, root) && loaded.empty(), "oversize load bounded");
  { std::ofstream file(root / "call" / first[0].id, std::ios::binary | std::ios::trunc); }
  Require(!Archive::Load("call", first[0].id, loaded, error, root), "empty load rejected");
  std::cout << "PASS: Unicode roundtrip, unique sorted records, context isolation, invalid paths, UTF-8 and size bounds\n";
 } catch (const std::exception& failure) { std::cerr << "FAIL: " << failure.what() << '\n'; std::wcerr << error << L'\n'; return 1; }
 return 0;
}
