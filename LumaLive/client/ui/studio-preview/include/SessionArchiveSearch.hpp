#pragma once
#include "SessionArchive.hpp"
#include <atomic>

namespace luma::client::ui::preview::SessionArchiveSearch {
struct Result {
 std::vector<SessionArchive::Entry> entries;
 size_t scanned = 0, skipped = 0;
 bool limited = false, cancelled = false;
 std::wstring error;
};
namespace detail {
inline bool Space(wchar_t value) {
 WORD kind = 0;
 return GetStringTypeW(CT_CTYPE1, &value, 1, &kind) && (kind & C1_SPACE);
}
inline bool Contains(const std::wstring& text, const std::wstring& query) {
 return !text.empty() && FindNLSStringEx(LOCALE_NAME_INVARIANT, FIND_FROMSTART | NORM_IGNORECASE,
  text.data(), static_cast<int>(text.size()), query.data(), static_cast<int>(query.size()), nullptr, nullptr, nullptr, 0) >= 0;
}
} // namespace detail
inline Result Search(const std::string& context, const std::wstring& query, const std::atomic<bool>& canceled,
 const std::filesystem::path& root = {}, size_t maxBytes = 32 * 1024 * 1024) {
 Result result;
 auto stopped = [&]() { return result.cancelled = canceled.load(std::memory_order_relaxed); };
 if (stopped()) return result;
 const auto first = std::find_if_not(query.begin(), query.end(), detail::Space);
 const auto last = std::find_if_not(query.rbegin(), query.rend(), detail::Space).base();
 const std::wstring needle = first < last ? std::wstring(first, last) : std::wstring();
 if (needle.size() > 256 || needle.find(L'\0') != std::wstring::npos) {
  result.error = UiLabel(L"\u641c\u7d22\u5185\u5bb9\u4e0d\u80fd\u8d85\u8fc7 256 \u4e2a\u5b57\u7b26\uff0c\u4e14\u4e0d\u80fd\u5305\u542b\u7a7a\u5b57\u7b26\u3002", L"Search text must be at most 256 characters and contain no null characters.");
  return result;
 }
 auto entries = SessionArchive::List(context, result.error, root);
 if (stopped() || !result.error.empty()) return result;
 // List exposes at most 1,000 records, without an overflow indicator.
 result.limited = entries.size() == 1000;
 if (needle.empty()) { result.entries = std::move(entries); return result; }
 if (entries.empty()) return result;
 std::filesystem::path directory; std::vector<SessionArchive::detail::Handle> locks;
 if (!SessionArchive::detail::Directory(context, root, false, directory, locks, result.error)) {
  if (result.error.empty()) result.error = UiLabel(L"\u5f52\u6863\u76ee\u5f55\u4e0d\u53ef\u7528\u3002", L"Archive directory is unavailable.");
  return result;
 }
 size_t consumed = 0;
 for (const auto& entry : entries) {
  if (stopped()) break;
  if (detail::Contains(entry.title, needle)) { ++result.scanned; result.entries.push_back(entry); continue; }
  // Keep the validated directory and read-only file handles open through each read.
  // No write/delete sharing means the measured size cannot change before ReadFile.
  SessionArchive::detail::Handle file(CreateFileW((directory / entry.id).c_str(), GENERIC_READ, FILE_SHARE_READ,
   nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
  if (!file || GetFileType(file.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.value, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      !GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > SessionArchive::MaxBytes) {
   ++result.scanned; ++result.skipped; continue;
  }
  const auto bytesToRead = static_cast<size_t>(size.QuadPart);
  if (bytesToRead > maxBytes - consumed) { result.limited = true; break; }
  if (stopped()) break;
  std::string text(bytesToRead, '\0'); DWORD bytes = 0;
  consumed += bytesToRead; ++result.scanned;
  const bool read = ReadFile(file.value, text.data(), static_cast<DWORD>(text.size()), &bytes, nullptr) && bytes == text.size();
  if (stopped()) break;
  if (!read || !SessionArchive::detail::Utf8(text)) { ++result.skipped; continue; }
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(count, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), count)) {
   ++result.skipped; continue;
  }
  if (detail::Contains(wide, needle)) result.entries.push_back(entry);
 }
 stopped();
 return result;
}
} // namespace luma::client::ui::preview::SessionArchiveSearch
