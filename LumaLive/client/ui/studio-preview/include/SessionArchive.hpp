#pragma once
#include "UiLocale.hpp"
#include <objbase.h>
#include <filesystem>
#include <vector>
#include <set>
#include <algorithm>
#include <utility>
#include <cwchar>

namespace luma::client::ui::preview::SessionArchive {
struct Entry { std::string id; std::wstring title; };
inline constexpr size_t MaxBytes = 1024 * 1024;
namespace detail {
inline bool Fail(std::wstring& error, const wchar_t* zh, const wchar_t* en) { error = UiLabel(zh, en); return false; }
struct Handle {
 HANDLE value = INVALID_HANDLE_VALUE;
 explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
 Handle(Handle&& h) noexcept : value(std::exchange(h.value, INVALID_HANDLE_VALUE)) {}
 Handle(const Handle&) = delete;
 ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
 explicit operator bool() const { return value != INVALID_HANDLE_VALUE; }
};
inline bool Utf8(const std::string& text) {
 return !text.empty() && text.size() <= MaxBytes && text.find('\0') == std::string::npos &&
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}
inline bool ValidId(const std::string& id) {
 if (id.size() != 59 || id[8] != 'T' || id[18] != 'Z' || id[19] != '_' || id.substr(56) != ".md") return false;
 for (size_t i = 0; i < 18; ++i) if (i != 8 && (id[i] < '0' || id[i] > '9')) return false;
 for (size_t i = 20; i < 56; ++i) {
  if (i == 28 || i == 33 || i == 38 || i == 43) { if (id[i] != '-') return false; }
  else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f') || (id[i] >= 'A' && id[i] <= 'F'))) return false;
 }
 return true;
}
// Keep every directory open without delete sharing until the operation completes.
// This prevents a checked ancestor from being replaced with a junction mid-operation.
inline bool Directory(const std::string& context, const std::filesystem::path& root, bool create,
 std::filesystem::path& directory, std::vector<Handle>& locks, std::wstring& error) {
 if (context != "call" && context != "meeting") return Fail(error, L"\u5f52\u6863\u7c7b\u578b\u65e0\u6548\u3002", L"Invalid archive context.");
 auto base = root;
 if (base.empty()) {
  DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  if (!count) return Fail(error, L"\u65e0\u6cd5\u627e\u5230\u672c\u5730\u5f52\u6863\u76ee\u5f55\u3002", L"Local archive directory is unavailable.");
  std::wstring value(count, L'\0'); DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), count);
  if (!written || written >= count) return Fail(error, L"\u65e0\u6cd5\u627e\u5230\u672c\u5730\u5f52\u6863\u76ee\u5f55\u3002", L"Local archive directory is unavailable.");
  value.resize(written); base = std::filesystem::path(value) / L"LumaLive" / L"records";
 }
 std::error_code ec; directory = std::filesystem::absolute(base, ec).lexically_normal() / context;
 if (ec) return Fail(error, L"\u5f52\u6863\u8def\u5f84\u65e0\u6548\u3002", L"Invalid archive directory.");
 auto prefix = directory.root_path();
 for (const auto& component : directory.relative_path()) {
  prefix /= component;
  if (create && !CreateDirectoryW(prefix.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
   return Fail(error, L"\u65e0\u6cd5\u521b\u5efa\u5f52\u6863\u76ee\u5f55\u3002", L"Could not create archive directory.");
  Handle handle(CreateFileW(prefix.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!handle) {
   DWORD code = GetLastError(); if (!create && (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)) return false;
   return Fail(error, L"\u65e0\u6cd5\u6253\u5f00\u5f52\u6863\u76ee\u5f55\u3002", L"Could not open archive directory.");
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(handle.value, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
   return Fail(error, L"\u5f52\u6863\u8def\u5f84\u4e0d\u80fd\u5305\u542b\u94fe\u63a5\u3002", L"Archive paths cannot contain links.");
  locks.push_back(std::move(handle));
 }
 return true;
}
inline std::wstring Title(const std::string& id) {
 std::wstring value(id.begin(), id.end());
 return value.substr(0,4) + L"-" + value.substr(4,2) + L"-" + value.substr(6,2) + L" " + value.substr(9,2) + L":" + value.substr(11,2) + L":" + value.substr(13,2) + L"." + value.substr(15,3) + L" UTC";
}
} // namespace detail
inline std::vector<Entry> List(const std::string& context, std::wstring& error, const std::filesystem::path& root = {}) {
 error.clear(); std::filesystem::path directory; std::vector<detail::Handle> locks;
 if (!detail::Directory(context, root, false, directory, locks, error)) return {};
 std::set<std::string, std::greater<>> newest; std::error_code ec;
 std::filesystem::directory_iterator it(directory, ec), end;
 while (!ec && it != end) {
  auto name = it->path().filename().native();
  if (name.size() == 59 && std::all_of(name.begin(), name.end(), [](wchar_t c) { return c >= 0 && c < 128; })) {
   std::string id(name.begin(), name.end()); DWORD attributes = GetFileAttributesW(it->path().c_str());
   if (detail::ValidId(id) && attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
    newest.insert(id); if (newest.size() > 1000) newest.erase(std::prev(newest.end()));
   }
  }
  it.increment(ec);
 }
 if (ec) { detail::Fail(error, L"\u65e0\u6cd5\u5217\u51fa\u5f52\u6863\u8bb0\u5f55\u3002", L"Could not list saved records."); return {}; }
 std::vector<Entry> entries; for (const auto& id : newest) entries.push_back({id, detail::Title(id)}); return entries;
}
inline bool Save(const std::string& context, const std::string& text, std::wstring& error, const std::filesystem::path& root = {}) {
 error.clear();
 if (!detail::Utf8(text)) return detail::Fail(error, L"\u8bb0\u5f55\u5fc5\u987b\u662f\u975e\u7a7a UTF-8 \u6587\u672c\uff0c\u4e14\u4e0d\u8d85\u8fc7 1 MiB\u3002", L"Records must contain valid UTF-8 text and be between 1 byte and 1 MiB.");
 std::filesystem::path directory; std::vector<detail::Handle> locks;
 if (!detail::Directory(context, root, true, directory, locks, error)) return false;
 GUID guid{}; wchar_t guidText[40]{};
 if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, guidText, 40)) return detail::Fail(error, L"\u65e0\u6cd5\u521b\u5efa\u8bb0\u5f55\u6807\u8bc6\u3002", L"Could not create record identifier.");
 SYSTEMTIME now{}; GetSystemTime(&now); wchar_t stamp[32]{};
 swprintf_s(stamp, L"%04u%02u%02uT%02u%02u%02u%03uZ_", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
 auto destination = directory / (std::wstring(stamp) + std::wstring(guidText + 1, 36) + L".md"); auto temporary = destination; temporary += L".tmp";
 bool written = false;
 {
  detail::Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file) return detail::Fail(error, L"\u65e0\u6cd5\u521b\u5efa\u5f52\u6863\u6587\u4ef6\u3002", L"Could not create archive file.");
  DWORD bytes = 0; written = WriteFile(file.value, text.data(), static_cast<DWORD>(text.size()), &bytes, nullptr) && bytes == text.size() && FlushFileBuffers(file.value);
 }
 if (written && MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
 DeleteFileW(temporary.c_str()); return detail::Fail(error, L"\u65e0\u6cd5\u4fdd\u5b58\u5f52\u6863\u8bb0\u5f55\u3002", L"Could not save archive record.");
}
inline bool Load(const std::string& context, const std::string& id, std::string& text, std::wstring& error, const std::filesystem::path& root = {}) {
 error.clear(); text.clear();
 if (!detail::ValidId(id)) return detail::Fail(error, L"\u5f52\u6863\u6807\u8bc6\u65e0\u6548\u3002", L"Invalid archive identifier.");
 std::filesystem::path directory; std::vector<detail::Handle> locks;
 if (!detail::Directory(context, root, false, directory, locks, error)) { if (error.empty()) detail::Fail(error, L"\u627e\u4e0d\u5230\u5f52\u6863\u8bb0\u5f55\u3002", L"Saved record was not found."); return false; }
 detail::Handle file(CreateFileW((directory / id).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
 BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
 if (!file || GetFileType(file.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.value, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || !GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > MaxBytes)
  return detail::Fail(error, L"\u65e0\u6cd5\u8bfb\u53d6\u8bb0\u5f55\uff0c\u6587\u4ef6\u65e0\u6548\u6216\u8d85\u8fc7 1 MiB\u3002", L"Cannot read record: file is unavailable, invalid, or exceeds 1 MiB.");
 std::string loaded(static_cast<size_t>(size.QuadPart), '\0'); DWORD bytes = 0;
 if (!ReadFile(file.value, loaded.data(), static_cast<DWORD>(loaded.size()), &bytes, nullptr) || bytes != loaded.size() || !detail::Utf8(loaded))
  return detail::Fail(error, L"\u5f52\u6863\u8bb0\u5f55\u4e0d\u662f\u6709\u6548\u7684 UTF-8 \u6587\u672c\u3002", L"Saved record could not be read as UTF-8 text.");
 text = std::move(loaded); return true;
}
inline bool Remove(const std::string& context, const std::string& id, std::wstring& error, const std::filesystem::path& root = {}) {
 error.clear();
 if (!detail::ValidId(id)) return detail::Fail(error, L"\u5f52\u6863\u6807\u8bc6\u65e0\u6548\u3002", L"Invalid archive identifier.");
 std::filesystem::path directory; std::vector<detail::Handle> locks;
 if (!detail::Directory(context, root, false, directory, locks, error)) { if (error.empty()) detail::Fail(error, L"\u627e\u4e0d\u5230\u5f52\u6863\u8bb0\u5f55\u3002", L"Saved record was not found."); return false; }
 detail::Handle file(CreateFileW((directory / id).c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
 if (!file) {
  DWORD code = GetLastError();
  if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
   return detail::Fail(error, L"\u627e\u4e0d\u5230\u5f52\u6863\u8bb0\u5f55\u3002", L"Saved record was not found.");
  return detail::Fail(error, L"\u65e0\u6cd5\u6253\u5f00\u5f52\u6863\u8bb0\u5f55\u4ee5\u5220\u9664\u3002", L"Could not open saved record for deletion.");
 }
 BY_HANDLE_FILE_INFORMATION info{};
 if (GetFileType(file.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.value, &info) ||
     (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
  return detail::Fail(error, L"\u65e0\u6cd5\u5220\u9664\u8bb0\u5f55\uff1a\u5f52\u6863\u6587\u4ef6\u65e0\u6548\u3002", L"Cannot delete record: archive file is invalid.");
 FILE_DISPOSITION_INFO disposition{TRUE};
 if (!SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)))
  return detail::Fail(error, L"\u65e0\u6cd5\u5220\u9664\u5f52\u6863\u8bb0\u5f55\u3002", L"Could not delete saved record.");
 return true;
}
} // namespace luma::client::ui::preview::SessionArchive
