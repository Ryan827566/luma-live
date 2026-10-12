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
inline std::wstring Title(const std::string& id, const TIME_ZONE_INFORMATION* timezone = nullptr) {
 const auto unavailable = UiLabel(L"\u65f6\u95f4\u4e0d\u53ef\u7528", L"Date unavailable");
 if (!ValidId(id)) return unavailable;
 auto number = [&](size_t start, size_t count) { WORD value = 0; for (size_t i = start; i < start + count; ++i) value = WORD(value * 10 + id[i] - '0'); return value; };
 SYSTEMTIME utc{}; utc.wYear = number(0,4); utc.wMonth = number(4,2); utc.wDay = number(6,2);
 utc.wHour = number(9,2); utc.wMinute = number(11,2); utc.wSecond = number(13,2); utc.wMilliseconds = number(15,3);
 SYSTEMTIME local{};
 // Use historical Windows timezone rules for saved records from earlier years.
 const bool converted = timezone ? SystemTimeToTzSpecificLocalTime(timezone, &utc, &local)
                                 : SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local);
 if (!converted) return unavailable;
 wchar_t stamp[40]{};
 swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u ", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond, local.wMilliseconds);
 return std::wstring(stamp) + UiLabel(L"\uff08\u672c\u5730\u65f6\u95f4\uff09", L"(local time)");
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
inline bool Export(const std::string& context, const std::string& id, const std::filesystem::path& destinationRoot,
 std::filesystem::path& exportedPath, std::wstring& error, const std::filesystem::path& recordsRoot = {}) {
 error.clear(); exportedPath.clear();
 if (destinationRoot.empty() || !destinationRoot.is_absolute() || destinationRoot.native().find(L'\0') != std::wstring::npos)
  return detail::Fail(error, L"\u8bf7\u9009\u62e9\u6709\u6548\u7684\u7edd\u5bf9\u5bfc\u51fa\u8def\u5f84\u3002", L"Choose a valid absolute export folder.");
 std::string text;
 if (!Load(context, id, text, error, recordsRoot)) return false;
 std::filesystem::path directory; std::vector<detail::Handle> locks;
 if (!detail::Directory(context, destinationRoot, true, directory, locks, error)) return false;
 const auto destination = directory / id;
 auto exists = [&]() { return detail::Fail(error, L"\u5bfc\u51fa\u76ee\u6807\u5df2\u5b58\u5728\uff0c\u8bf7\u9009\u62e9\u5176\u4ed6\u6587\u4ef6\u5939\u3002", L"Export destination already exists. Choose another folder."); };
 if (GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES) return exists();
 GUID guid{}; wchar_t guidText[40]{};
 if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, guidText, 40))
  return detail::Fail(error, L"\u65e0\u6cd5\u521b\u5efa\u5bfc\u51fa\u6587\u4ef6\u3002", L"Could not create export file.");
 const auto temporary = directory / (std::wstring(L".export-") + guidText + L".tmp");
 detail::Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
 if (!file) return detail::Fail(error, L"\u65e0\u6cd5\u521b\u5efa\u5bfc\u51fa\u6587\u4ef6\u3002", L"Could not create export file.");
 // Delete only the file we created, through its still-exclusive handle, on failure.
 auto discard = [&]() { FILE_DISPOSITION_INFO info{TRUE}; SetFileInformationByHandle(file.value, FileDispositionInfo, &info, sizeof(info)); };
 DWORD bytes = 0;
 if (!WriteFile(file.value, text.data(), static_cast<DWORD>(text.size()), &bytes, nullptr) || bytes != text.size() || !FlushFileBuffers(file.value)) {
  discard(); return detail::Fail(error, L"\u65e0\u6cd5\u5199\u5165\u5bfc\u51fa\u6587\u4ef6\u3002", L"Could not write export file.");
 }
 // Rename without releasing the handle or replacing any existing destination.
 const auto& name = destination.native();
 std::vector<BYTE> buffer(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t), 0);
 auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
 rename->ReplaceIfExists = FALSE; rename->RootDirectory = nullptr;
 rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
 std::copy(name.begin(), name.end(), rename->FileName);
 if (!SetFileInformationByHandle(file.value, FileRenameInfo, rename, static_cast<DWORD>(buffer.size()))) {
  const DWORD code = GetLastError(); discard();
  if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS || GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES) return exists();
  return detail::Fail(error, L"\u65e0\u6cd5\u5b8c\u6210\u5f52\u6863\u5bfc\u51fa\u3002", L"Could not complete archive export.");
 }
 exportedPath = destination; return true;
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
