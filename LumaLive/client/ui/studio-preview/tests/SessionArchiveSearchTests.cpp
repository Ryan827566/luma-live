#include "SessionArchiveSearch.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Archive = luma::client::ui::preview::SessionArchive;
namespace Search = luma::client::ui::preview::SessionArchiveSearch;
using luma::client::ui::preview::UiUtf8;
namespace fs = std::filesystem;
static void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int wmain() {
 SetEnvironmentVariableW(L"LUMALIVE_UI_LANGUAGE", L"en-US");
 GUID guid{}; wchar_t guidText[40]{};
 if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, guidText, 40)) return 1;
 const auto root = fs::temp_directory_path() / (std::wstring(L"LumaLiveArchiveSearchTests-") + guidText);
 if (!fs::create_directory(root)) return 1;
 struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
 try {
  std::atomic<bool> canceled{false}; std::wstring error;
  auto empty = Search::Search("call", L"", canceled, root);
  Require(empty.entries.empty() && empty.error.empty() && !fs::exists(root / "call"), "missing archive search does not create directories");
  const std::string older = "20261010T120000000Z_00000000-0000-0000-0000-000000000001.md";
  const std::string newer = "20261011T120000000Z_00000000-0000-0000-0000-000000000002.md";
  const std::string broken = "20261012T120000000Z_00000000-0000-0000-0000-000000000003.md";
  const auto unicode = UiUtf8(L"# Alpha \u4f1a\u8bae\u7eaa\u8981\nCAF\u00c9 \u041f\u0420\u0418\u0412\u0415\u0422 \U0001f30f\n");
  fs::create_directory(root / "call"); fs::create_directory(root / "meeting");
  auto write = [](const fs::path& path, const std::string& body) {
   std::ofstream file(path, std::ios::binary); file << body; Require(file.good(), "fixture write");
  };
  write(root / "call" / older, unicode); write(root / "call" / newer, "ALPHA newer");
  write(root / "meeting" / newer, "MeetingOnly");
  const auto timestamp = fs::last_write_time(root / "call" / older);
  for (const auto* query : {L"\t alpha \u3000", L"ALPHA"}) {
   const auto result = Search::Search("call", query, canceled, root);
   Require(result.entries.size() == 2 && result.entries[0].id == newer && result.entries[1].id == older &&
       result.scanned == 2 && !result.limited && !result.cancelled && result.error.empty(), "trimmed case-insensitive body match in newest order");
  }
  for (const auto* query : {L"\u4f1a\u8bae", L"caf\u00e9", L"\u043f\u0440\u0438\u0432\u0435\u0442", L"\U0001f30f"}) {
   const auto result = Search::Search("call", query, canceled, root);
   Require(result.entries.size() == 1 && result.entries[0].id == older, "Unicode and non-ASCII case match");
  }
  const auto listed = Archive::List("call", error, root);
  const auto date = Search::Search("call", listed[0].title, canceled, root);
  Require(date.entries.size() == 1 && date.entries[0].id == newer, "localized date title match");
  const auto allTitles = Search::Search("call", L"LOCAL TIME", canceled, root, 0);
  Require(allTitles.entries.size() == 2 && !allTitles.limited, "title matching uses no body budget");
  const auto blank = Search::Search("call", L" \t\r\n\u3000", canceled, root, 0);
  Require(blank.entries.size() == listed.size() && blank.entries[0].id == listed[0].id && !blank.limited, "empty query restores archive list");
  const auto absent = Search::Search("call", L"NotPresentAnywhere", canceled, root);
  Require(absent.entries.empty() && absent.scanned == 2 && absent.skipped == 0 && !absent.limited && absent.error.empty(), "complete no-match search");
  Require(Search::Search("call", L"MeetingOnly", canceled, root).entries.empty() &&
      Search::Search("meeting", L"meetingonly", canceled, root).entries.size() == 1, "context isolation");
  canceled.store(true);
  const auto stopped = Search::Search("call", L"Alpha", canceled, root);
  Require(stopped.cancelled && stopped.scanned == 0 && stopped.entries.empty(), "cancel before enumeration");
  canceled.store(false);
  const auto budget = Search::Search("call", L"alpha", canceled, root, 11);
  Require(budget.limited && budget.scanned == 1 && budget.entries.size() == 1 && budget.entries[0].id == newer, "exact byte budget retains newest partial match");
  const auto noBudget = Search::Search("call", L"missing", canceled, root, 0);
  Require(noBudget.limited && noBudget.scanned == 0 && noBudget.entries.empty(), "budget exhaustion never claims complete no matches");
  const auto exact = Search::Search("call", L"alpha", canceled, root, 11 + unicode.size());
  Require(!exact.limited && exact.entries.size() == 2, "exact total byte budget completes");
  Require(!Search::Search("call", std::wstring(257, L'a'), canceled, root).error.empty(), "oversize query rejected");
  Require(!Search::Search("call", std::wstring(L"a\0b", 3), canceled, root).error.empty(), "embedded null query rejected");
  Require(!Search::Search("../call", L"alpha", canceled, root).error.empty(), "invalid context reports list error");
  write(root / "bad-root", "not a directory");
  Require(!Search::Search("call", L"alpha", canceled, root / "bad-root").error.empty(), "invalid archive path reports error");
  write(root / "call" / broken, std::string("\xff", 1));
  const auto invalid = Search::Search("call", L"alpha", canceled, root);
  Require(invalid.skipped == 1 && invalid.scanned == 3 && invalid.entries.size() == 2, "invalid UTF-8 is skipped without losing readable matches");
  {
   Archive::detail::Handle locked(CreateFileW((root / "call" / newer).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
   Require(static_cast<bool>(locked), "exclusive lock fixture");
   const auto unreadable = Search::Search("call", L"alpha", canceled, root);
   Require(unreadable.skipped == 2 && unreadable.entries.size() == 1 && unreadable.entries[0].id == older, "unreadable record skipped");
  }
  Require(fs::last_write_time(root / "call" / older) == timestamp, "search preserves source modified time");
  const auto cappedRoot = root / "capped"; fs::create_directories(cappedRoot / "call");
  for (int i = 0; i < 1001; ++i) {
   char suffix[64]{}; sprintf_s(suffix, "20261010T120000000Z_00000000-0000-0000-0000-%012x.md", i);
   write(cappedRoot / "call" / suffix, "cap");
  }
  const auto cap = Search::Search("call", L"cap", canceled, cappedRoot);
  Require(cap.entries.size() == 1000 && cap.scanned == 1000 && cap.limited, "newest thousand cap is explicit");
  Require(cap.entries.front().id > cap.entries.back().id && cap.entries.back().id != "20261010T120000000Z_00000000-0000-0000-0000-000000000000.md", "oldest record excluded by cap");
  std::cout << "PASS: Unicode/date search, contexts, empty/no matches, cancellation, byte/record limits, unreadable UTF-8 and invalid paths\n";
 } catch (const std::exception& failure) { std::cerr << "FAIL: " << failure.what() << '\n'; return 1; }
 return 0;
}
