#include "SessionArchive.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
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
  const std::string dated = "20261010T203000123Z_00000000-0000-0000-0000-000000000000.md";
  TIME_ZONE_INFORMATION east{}; east.Bias = -480;
  Require(Archive::detail::Title(dated, &east) == L"2026-10-11 04:30:00.123 (local time)", "local timestamp crosses UTC midnight and retains milliseconds");
  TIME_ZONE_INFORMATION west{}; west.Bias = 480;
  Require(Archive::detail::Title(dated, &west) == L"2026-10-10 12:30:00.123 (local time)", "western local timestamp");
  SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE", L"zh-CN");
  Require(Archive::detail::Title(dated, &east).find(L"\u672c\u5730\u65f6\u95f4") != std::wstring::npos, "simplified local timestamp label");
  SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE", L"zh-TW");
  Require(Archive::detail::Title(dated, &east).find(L"\u672c\u5730\u6642\u9593") != std::wstring::npos, "traditional local timestamp label");
  SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE", L"en-US");
  Require(Archive::detail::Title("20261340" + dated.substr(8), &east) == L"Date unavailable", "invalid timestamp is not mislabeled as local time");
  Require(Archive::List("call", error, root).empty() && error.empty() && !fs::exists(root / "call"), "empty listing must not create directories");
  const std::string unicode = UiUtf8(L"# \u4f1a\u8bae\u8bb0\u5f55 \U0001f30f\r\n\n\u4f60\u597d\uff0c\u4e16\u754c\u3002\n");
  Require(Archive::Save("call", unicode, error, root) && error.empty(), "unicode save");
  auto first = Archive::List("call", error, root);
  Require(first.size() == 1 && !first[0].title.empty() && error.empty(), "first record listing");
  Require(Archive::Save("call", unicode, error, root), "second save");
  auto records = Archive::List("call", error, root);
  Require(records.size() == 2 && records[0].id != records[1].id && records[0].id > records[1].id, "unique newest-first ids");
  std::string loaded;
  Require(Archive::Load("call", first[0].id, loaded, error, root) && loaded == unicode, "unicode roundtrip");
  auto readBytes = [](const fs::path& path) { std::ifstream file(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); };
  const auto source = root / "call" / first[0].id;
  const auto sourceTime = fs::last_write_time(source);
  const auto exportRoot = root / L"\u5bfc\u51fa \u8bb0\u5f55";
  fs::path exported = L"previous-result";
  error = L"previous error";
  Require(Archive::Export("call", first[0].id, exportRoot, exported, error, root) && error.empty() &&
      exported == exportRoot / "call" / first[0].id, "export returns contextual destination and clears error");
  Require(readBytes(exported) == unicode, "export preserves Unicode and mixed newline bytes exactly");
  Require(readBytes(source) == unicode && fs::last_write_time(source) == sourceTime, "export does not mutate source bytes or modified time");
  { std::ofstream file(exported, std::ios::binary | std::ios::trunc); file << "Existing export must survive\r\n"; }
  Require(!Archive::Export("call", first[0].id, exportRoot, exported, error, root) && exported.empty() &&
      error == L"Export destination already exists. Choose another folder.", "duplicate export clearly rejected");
  Require(readBytes(exportRoot / "call" / first[0].id) == "Existing export must survive\r\n", "duplicate export preserves existing destination");
  Require(std::distance(fs::directory_iterator(exportRoot / "call"), fs::directory_iterator()) == 1, "export leaves no temporary files");
  for (const auto& invalidRoot : {fs::path(), fs::path("relative-export"), fs::path("D:relative-export")})
   Require(!Archive::Export("call", first[0].id, invalidRoot, exported, error, root) && exported.empty() && !error.empty(), "export requires explicit absolute folder");
  for (const auto& id : {"../record.md", "..\\record.md", "record.md", ""})
   Require(!Archive::Export("call", id, exportRoot, exported, error, root) && exported.empty() && !error.empty(), "export rejects invalid identifiers");
  Require(!Archive::Export("../call", first[0].id, exportRoot, exported, error, root) && !error.empty(), "export rejects invalid context");
  const auto blockedRoot = root / "blocked-export";
  Require(fs::create_directories(blockedRoot / "call" / first[0].id), "export destination directory fixture");
  Require(!Archive::Export("call", first[0].id, blockedRoot, exported, error, root) &&
      fs::is_directory(blockedRoot / "call" / first[0].id), "export preserves record-shaped destination directory");
  const auto fileRoot = root / "file-export-root";
  { std::ofstream file(fileRoot, std::ios::binary); file << "Not a folder"; }
  Require(!Archive::Export("call", first[0].id, fileRoot, exported, error, root) && readBytes(fileRoot) == "Not a folder", "export rejects file ancestor");
  const auto linkRoot = root / "linked-export";
  const auto outside = root / "link-target"; Require(fs::create_directory(outside), "link destination fixture");
  if (CreateSymbolicLinkW(linkRoot.c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
   Require(!Archive::Export("call", first[0].id, linkRoot, exported, error, root) && !fs::exists(outside / "call"), "export rejects linked destination ancestor");
   fs::remove(linkRoot);
  } else std::cout << "SKIP: destination ancestor symlink fixture requires Windows symlink permission\n";
  const auto linkedId = records[0].id == first[0].id ? records[1].id : records[0].id;
  const auto linkedDestination = blockedRoot / "call" / linkedId;
  if (CreateSymbolicLinkW(linkedDestination.c_str(), source.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
   Require(!Archive::Export("call", linkedId, blockedRoot, exported, error, root) && readBytes(source) == unicode, "export rejects linked destination file");
   fs::remove(linkedDestination);
  } else std::cout << "SKIP: destination file symlink fixture requires Windows symlink permission\n";
  Require(Archive::List("meeting", error, root).empty(), "context listing isolation");
  Require(!Archive::Load("meeting", first[0].id, loaded, error, root) && loaded.empty() && !error.empty(), "context load isolation");
  Require(Archive::Save("meeting", "# Meeting\n", error, root) && Archive::List("meeting", error, root).size() == 1, "meeting save");
  for (const auto& id : {"../record.md", "..\\record.md", "C:\\record.md", "record.md", ""}) {
   Require(!Archive::Load("call", id, loaded, error, root) && !error.empty(), "invalid id rejected");
   Require(!Archive::Remove("call", id, error, root) && !error.empty(), "invalid remove id rejected");
  }
  Require(!Archive::Remove("../call", first[0].id, error, root) && !error.empty(), "invalid remove context rejected");
  Require(Archive::Load("call", first[0].id, loaded, error, root) && loaded == unicode, "rejected removal preserves record");
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
  { std::ofstream file(source, std::ios::binary | std::ios::trunc); file << '\xff'; }
  Require(!Archive::Export("call", first[0].id, root / "invalid-text-export", exported, error, root) &&
      exported.empty() && !error.empty() && !fs::exists(root / "invalid-text-export"), "export rejects invalid UTF-8 before creating destination");
  const auto meetingBefore = Archive::List("meeting", error, root);
  const auto retainedId = records[0].id == first[0].id ? records[1].id : records[0].id;
  error = L"previous error";
  Require(Archive::Remove("call", first[0].id, error, root) && error.empty(), "selected record removed");
  Require(!fs::exists(root / "call" / first[0].id), "selected file no longer exists");
  const auto remaining = Archive::List("call", error, root);
  Require(remaining.size() == 1 && remaining[0].id == retainedId, "only selected record removed");
  Require(Archive::Load("call", retainedId, loaded, error, root) && loaded == unicode, "other record content preserved");
  const auto meetingAfter = Archive::List("meeting", error, root);
  Require(meetingBefore.size() == 2 && meetingAfter.size() == meetingBefore.size(), "other context count preserved");
  for (size_t i = 0; i < meetingBefore.size(); ++i) {
   Require(meetingAfter[i].id == meetingBefore[i].id, "other context identifiers preserved");
   Require(Archive::Load("meeting", meetingAfter[i].id, loaded, error, root) &&
       (loaded == "# Meeting\n" || loaded == boundary), "other context contents preserved");
  }
  Require(!Archive::Remove("call", first[0].id, error, root) && error == L"Saved record was not found.", "repeated removal reports missing record");
  Require(!Archive::Remove("call", retainedId, error, root / "missing") && error == L"Saved record was not found." &&
      !fs::exists(root / "missing"), "missing directory removal fails without creating it");
  Require(fs::create_directory(root / "call" / first[0].id), "directory rejection fixture");
  Require(!Archive::Remove("call", first[0].id, error, root) && !error.empty() &&
      fs::is_directory(root / "call" / first[0].id), "record-shaped directory cannot be removed");
  std::cout << "PASS: Unicode roundtrip, unique sorted records, context isolation, invalid paths, UTF-8 and size bounds, safe selected removal and non-overwriting export\n";
 } catch (const std::exception& failure) { std::cerr << "FAIL: " << failure.what() << '\n'; std::wcerr << error << L'\n'; return 1; }
 return 0;
}
