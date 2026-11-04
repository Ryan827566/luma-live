#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifdef _WIN32
#include "UiLocale.hpp"
#include <wincrypt.h>
#include <objbase.h>
#endif

namespace luma::client::ui::preview {
struct PreviewNetworkSettings {
    bool enabled{false};
    std::wstring stunUrl, turnUrl, turnUsername, turnPassword;
};

namespace PreviewNetworkSettingsDetail {
constexpr std::size_t FileLimit = 65536;
constexpr std::size_t UrlLimit = 2048, UsernameLimit = 1024, PasswordLimit = 4096;
inline bool Fail(std::wstring& error, const wchar_t* zh, const wchar_t* en) {
#ifdef _WIN32
    error = UiLabel(zh, en);
#else
    (void)zh;
    error = en;
#endif
    return false;
}
inline auto Fields(PreviewNetworkSettings& settings) {
    return std::array<std::wstring*, 4>{&settings.stunUrl, &settings.turnUrl,
        &settings.turnUsername, &settings.turnPassword};
}
inline auto Fields(const PreviewNetworkSettings& settings) {
    return std::array<const std::wstring*, 4>{&settings.stunUrl, &settings.turnUrl,
        &settings.turnUsername, &settings.turnPassword};
}
inline bool ValidText(std::wstring_view text, std::size_t limit) {
    if (text.size() > limit) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<std::uint32_t>(text[i]);
        if (c < 32 || (c >= 127 && c <= 159) || c == 0x2028 || c == 0x2029) return false;
        if constexpr (sizeof(wchar_t) == 2) {
            if (c >= 0xd800 && c <= 0xdbff) {
                if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
            } else if (c >= 0xdc00 && c <= 0xdfff) return false;
        } else if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
inline bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
inline bool Alpha(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}
inline bool Ipv4(std::wstring_view host) {
    unsigned groups = 0;
    while (!host.empty()) {
        const auto dot = host.find(L'.');
        const auto part = host.substr(0, dot);
        if (part.empty() || part.size() > 3 || (part.size() > 1 && part.front() == L'0')) return false;
        unsigned value = 0;
        for (const auto c : part) { if (!Digit(c)) return false; value = value * 10 + c - L'0'; }
        if (value > 255 || ++groups > 4) return false;
        if (dot == host.npos) return groups == 4;
        host.remove_prefix(dot + 1);
    }
    return false;
}
inline bool Ipv6Part(std::wstring_view part, bool last, unsigned& groups) {
    while (!part.empty()) {
        const auto colon = part.find(L':');
        const auto token = part.substr(0, colon);
        if (token.empty()) return false;
        if (token.find(L'.') != token.npos) {
            if (!last || colon != part.npos || !Ipv4(token)) return false;
            groups += 2;
        } else {
            if (token.size() > 4) return false;
            for (const auto c : token)
                if (!Digit(c) && !(c >= L'a' && c <= L'f') && !(c >= L'A' && c <= L'F')) return false;
            ++groups;
        }
        if (colon == part.npos) return true;
        part.remove_prefix(colon + 1);
        if (part.empty()) return false;
    }
    return true;
}
inline bool Ipv6(std::wstring_view host) {
    if (host.empty()) return false;
    unsigned groups = 0;
    const auto compressed = host.find(L"::");
    if (compressed == host.npos) return Ipv6Part(host, true, groups) && groups == 8;
    if (host.find(L"::", compressed + 2) != host.npos) return false;
    return Ipv6Part(host.substr(0, compressed), false, groups) &&
        Ipv6Part(host.substr(compressed + 2), true, groups) && groups < 8;
}
inline bool DnsOrIpv4(std::wstring_view host) {
    if (host.empty() || host.size() > 254) return false;
    const bool numeric = std::all_of(host.begin(), host.end(), [](wchar_t c) { return Digit(c) || c == L'.'; });
    if (numeric) return Ipv4(host);
    if (host.back() == L'.') host.remove_suffix(1);
    if (host.empty() || host.size() > 253) return false;
    while (!host.empty()) {
        const auto dot = host.find(L'.');
        const auto label = host.substr(0, dot);
        if (label.empty() || label.size() > 63 || label.front() == L'-' || label.back() == L'-') return false;
        for (auto c : label) if (!Alpha(c) && !Digit(c) && c != L'-') return false;
        if (dot == host.npos) return true;
        host.remove_prefix(dot + 1);
        if (host.empty()) return false;
    }
    return false;
}
// RFC 7064/7065 are non-hierarchical URIs. This supported subset accepts DNS
// (ASCII/punycode), IPv4 and bracketed IPv6; TURN transport is UDP or TCP.
inline bool Uri(std::wstring_view uri, bool turn) {
    if (uri.empty()) return true;
    for (auto c : uri) if (c <= 32 || c >= 127) return false;
    const auto colon = uri.find(L':');
    if (colon == uri.npos) return false;
    const auto scheme = uri.substr(0, colon);
    if (turn ? (scheme != L"turn" && scheme != L"turns") : (scheme != L"stun")) return false;
    auto authority = uri.substr(colon + 1);
    const auto query = authority.find(L'?');
    if (query != authority.npos) {
        if (!turn) return false;
        const auto transport = authority.substr(query);
        if (transport != L"?transport=udp" && transport != L"?transport=tcp") return false;
        if (scheme == L"turns" && transport == L"?transport=udp") return false;
        authority = authority.substr(0, query);
    }
    if (authority.empty()) return false;
    std::wstring_view port;
    if (authority.front() == L'[') {
        const auto close = authority.find(L']');
        if (close == authority.npos || !Ipv6(authority.substr(1, close - 1))) return false;
        const auto suffix = authority.substr(close + 1);
        if (suffix.empty()) return true;
        if (suffix.front() != L':') return false;
        port = suffix.substr(1);
    } else {
        const auto split = authority.find(L':');
        if (!DnsOrIpv4(authority.substr(0, split))) return false;
        if (split == authority.npos) return true;
        port = authority.substr(split + 1);
    }
    if (port.empty() || port.size() > 5) return false;
    unsigned number = 0;
    for (auto c : port) { if (!Digit(c)) return false; number = number * 10 + c - L'0'; }
    return number > 0 && number <= 65535;
}
}

inline bool ValidatePreviewNetworkSettings(const PreviewNetworkSettings& settings, std::wstring& error) {
    using namespace PreviewNetworkSettingsDetail;
    error.clear();
    if (!ValidText(settings.stunUrl, UrlLimit) || !ValidText(settings.turnUrl, UrlLimit) ||
        !ValidText(settings.turnUsername, UsernameLimit) || !ValidText(settings.turnPassword, PasswordLimit))
        return Fail(error, L"网络设置包含过长字段、控制字符或无效文字。", L"Network settings contain an oversized field, control character or invalid text.");
    if (!Uri(settings.stunUrl, false))
        return Fail(error, L"STUN 地址格式无效。请使用 stun:主机:端口，IPv6 地址需加方括号。", L"Invalid STUN URI. Use stun:host:port and brackets around IPv6 addresses.");
    if (!Uri(settings.turnUrl, true))
        return Fail(error, L"TURN 地址格式无效。请使用 turn: 或 turns:；仅支持 UDP/TCP，TLS 使用 TCP。", L"Invalid TURN URI. Use turn: or turns:; UDP/TCP are supported, with TCP for TLS.");
    if (settings.turnUrl.empty() && (!settings.turnUsername.empty() || !settings.turnPassword.empty()))
        return Fail(error, L"填写 TURN 凭据时必须同时填写 TURN 地址。", L"A TURN URI is required when TURN credentials are set.");
    if (settings.turnUsername.empty() != settings.turnPassword.empty())
        return Fail(error, L"TURN 用户名与密码需同时填写，或同时留空。", L"Set both TURN username and password, or leave both empty.");
    return true;
}

#ifdef _WIN32
namespace PreviewNetworkSettingsDetail {
inline std::filesystem::path Path(const std::filesystem::path& custom) {
    if (!custom.empty()) return custom;
    wchar_t local[32768]{};
    const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (!count || count >= 32768) return {};
    return std::filesystem::path(local) / L"LumaLive" / L"rtc-network.dat";
}
inline void Clear(PreviewNetworkSettings& settings) {
    for (auto* field : Fields(settings)) if (!field->empty()) SecureZeroMemory(field->data(), field->size() * sizeof(wchar_t));
    settings = {};
}
struct Scratch {
    PreviewNetworkSettings value;
    ~Scratch() { Clear(value); }
};
struct Plaintext {
    std::string value;
    ~Plaintext() { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); }
};
struct Blob {
    DATA_BLOB value{};
    ~Blob() { if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); } }
};
struct File {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~File() { Close(); }
    void Close() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); value = INVALID_HANDLE_VALUE; } }
};
struct TemporaryFile {
    std::filesystem::path value;
    bool owned{false};
    ~TemporaryFile() { if (owned && !value.empty()) DeleteFileW(value.c_str()); }
};
inline bool InvalidSaved(std::wstring& error) {
    return Fail(error, L"已保存的网络设置损坏，正在使用环境默认值。请检查并重新保存。", L"Saved network settings are invalid. Environment defaults are in use. Review and save the settings again.");
}
inline constexpr char Magic[] = "LumaLiveRtc1";
inline void AppendSize(std::string& bytes, std::uint32_t size) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<char>((size >> shift) & 255));
}
inline bool Encode(const PreviewNetworkSettings& settings, Plaintext& plain) {
    // Reserve once so previous plaintext allocations need not be left behind.
    plain.value.reserve(FileLimit / 2);
    plain.value.append(Magic, sizeof(Magic));
    plain.value.push_back(settings.enabled ? '\1' : '\0');
    for (const auto* field : Fields(settings)) {
        const int count = field->empty() ? 0 : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            field->data(), static_cast<int>(field->size()), nullptr, 0, nullptr, nullptr);
        if (!field->empty() && count <= 0) return false;
        if (plain.value.size() + 4 + count > FileLimit / 2) return false;
        AppendSize(plain.value, static_cast<std::uint32_t>(count));
        const auto offset = plain.value.size();
        plain.value.resize(offset + count);
        if (count && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, field->data(),
            static_cast<int>(field->size()), plain.value.data() + offset, count, nullptr, nullptr) != count) return false;
    }
    return true;
}
inline bool Decode(const DATA_BLOB& blob, PreviewNetworkSettings& settings) {
    if (blob.cbData < sizeof(Magic) + 1 || blob.cbData > FileLimit / 2) return false;
    const std::string_view bytes(reinterpret_cast<const char*>(blob.pbData), blob.cbData);
    if (bytes.substr(0, sizeof(Magic)) != std::string_view(Magic, sizeof(Magic))) return false;
    std::size_t offset = sizeof(Magic);
    const auto enabled = static_cast<unsigned char>(bytes[offset++]);
    if (enabled > 1) return false;
    settings.enabled = enabled != 0;
    for (auto* field : Fields(settings)) {
        if (bytes.size() - offset < 4) return false;
        std::uint32_t length = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) length |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset++])) << shift;
        if (length > bytes.size() - offset) return false;
        const char* value = bytes.data() + offset;
        const int count = length ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, static_cast<int>(length), nullptr, 0) : 0;
        if (length && count <= 0) return false;
        field->resize(count);
        if (count && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, static_cast<int>(length), field->data(), count) != count) return false;
        offset += length;
    }
    return offset == bytes.size();
}
}
#endif

inline bool SavePreviewNetworkSettings(const PreviewNetworkSettings& settings, std::wstring& error,
    const std::filesystem::path& custom = {}) {
    using namespace PreviewNetworkSettingsDetail;
    error.clear();
    if (!ValidatePreviewNetworkSettings(settings, error)) return false;
#ifdef _WIN32
    try {
        Plaintext plain;
        if (!Encode(settings, plain)) return Fail(error, L"网络设置包含无效文字。", L"Network settings contain invalid text.");
        DATA_BLOB source{static_cast<DWORD>(plain.value.size()), reinterpret_cast<BYTE*>(plain.value.data())};
        Blob encrypted;
        if (!CryptProtectData(&source, L"LumaLive RTC network", nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &encrypted.value) || encrypted.value.cbData > FileLimit)
            return Fail(error, L"无法加密网络设置，请稍后重试。", L"Could not encrypt network settings. Try again.");
        const auto file = Path(custom);
        if (file.empty()) return Fail(error, L"找不到用户设置目录。", L"User settings directory is unavailable.");
        if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
        GUID guid{};
        wchar_t id[40]{};
        if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, id, 40))
            return Fail(error, L"无法创建网络设置文件。", L"Could not create the network settings file.");
        TemporaryFile temporary{file};
        temporary.value += std::wstring(L".") + id + L".tmp";
        File output{CreateFileW(temporary.value.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (output.value == INVALID_HANDLE_VALUE)
            return Fail(error, L"无法写入网络设置目录。", L"Could not write to the network settings directory.");
        temporary.owned = true;
        DWORD written = 0;
        const bool ready = WriteFile(output.value, encrypted.value.pbData, encrypted.value.cbData, &written, nullptr) &&
            written == encrypted.value.cbData && FlushFileBuffers(output.value);
        output.Close();
        if (!ready || !MoveFileExW(temporary.value.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return Fail(error, L"保存失败，原网络设置已保留。", L"Save failed. Previous network settings were retained.");
        temporary.value.clear();
        return true;
    } catch (...) {
        return Fail(error, L"无法保存网络设置，请检查目录权限。原设置已保留。", L"Could not save network settings. Check directory permissions. Previous settings were retained.");
    }
#else
    (void)custom;
    return Fail(error, L"加密网络设置仅支持 Windows。", L"Encrypted network settings are supported only on Windows.");
#endif
}

inline bool LoadPreviewNetworkSettings(PreviewNetworkSettings& settings, std::wstring& error,
    const std::filesystem::path& custom = {}) {
    using namespace PreviewNetworkSettingsDetail;
#ifdef _WIN32
    Clear(settings);
#else
    settings = {};
#endif
    error.clear();
#ifdef _WIN32
    try {
        const auto file = Path(custom);
        if (file.empty()) return Fail(error, L"找不到用户设置目录，正在使用环境默认值。", L"User settings directory is unavailable. Environment defaults are in use.");
        File input{CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (input.value == INVALID_HANDLE_VALUE) {
            const auto code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
            return Fail(error, L"无法读取网络设置，正在使用环境默认值。", L"Could not read network settings. Environment defaults are in use.");
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(input.value, &size) || size.QuadPart <= 0 || size.QuadPart > FileLimit) return InvalidSaved(error);
        std::vector<BYTE> bytes(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(input.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())
            return Fail(error, L"无法读取网络设置，正在使用环境默认值。", L"Could not read network settings. Environment defaults are in use.");
        input.Close();
        DATA_BLOB source{static_cast<DWORD>(bytes.size()), bytes.data()};
        Blob decoded;
        if (!CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &decoded.value))
            return Fail(error, L"无法解密网络设置，正在使用环境默认值。请使用保存时的 Windows 用户或重新保存。", L"Could not decrypt network settings. Environment defaults are in use. Use the Windows account that saved them or save them again.");
        Scratch result;
        if (!Decode(decoded.value, result.value) || !ValidatePreviewNetworkSettings(result.value, error)) return InvalidSaved(error);
        settings = std::move(result.value);
        return true;
    } catch (...) {
        Clear(settings);
        return Fail(error, L"无法读取网络设置，正在使用环境默认值。请检查并重新保存。", L"Could not read network settings. Environment defaults are in use. Review and save the settings again.");
    }
#else
    (void)custom;
    return Fail(error, L"加密网络设置仅支持 Windows。", L"Encrypted network settings are supported only on Windows.");
#endif
}
}
