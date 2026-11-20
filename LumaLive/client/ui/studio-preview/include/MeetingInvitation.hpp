#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace luma::client::ui::preview {
struct MeetingInvitation {
    std::wstring server;
    std::wstring room;
};

namespace MeetingInvitationDetail {
inline constexpr std::size_t TextLimit = 4096;
inline constexpr std::size_t RoomByteLimit = 128;

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
        for (auto c : part) {
            if (!Digit(c)) return false;
            value = value * 10 + c - L'0';
        }
        if (value > 255 || ++groups > 4) return false;
        if (dot == host.npos) return groups == 4;
        host.remove_prefix(dot + 1);
    }
    return false;
}
inline bool Host(std::wstring_view host) {
    if (host.empty() || host.size() > 254) return false;
    bool numeric = true;
    for (auto c : host) if (!Digit(c) && c != L'.') numeric = false;
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
    }
    return false;
}
inline bool Server(std::wstring_view server) {
    // The current meeting Join handler splits at the last colon and does not
    // remove IPv6 brackets. Accept DNS/IPv4 with an optional decimal port only.
    const auto colon = server.find(L':');
    if (!Host(server.substr(0, colon))) return false;
    if (colon == server.npos) return true; // Join uses port 9000.
    const auto port = server.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned value = 0;
    for (auto c : port) {
        if (!Digit(c)) return false;
        value = value * 10 + c - L'0';
    }
    return value > 0 && value <= 65535;
}
inline bool Room(std::wstring_view room) {
    // MeetingSession::Start and server MeetingService limit UTF-8 bytes.
    if (room.empty() || room.size() > RoomByteLimit) return false;
    std::size_t bytes = 0;
    bool nonSpace = false;
    for (std::size_t i = 0; i < room.size(); ++i) {
        auto c = static_cast<std::uint32_t>(room[i]);
        if (c < 32 || (c >= 127 && c <= 159) || c == 0x2028 || c == 0x2029) return false;
        if constexpr (sizeof(wchar_t) == 2) {
            if (c >= 0xd800 && c <= 0xdbff) {
                if (++i == room.size()) return false;
                const auto low = static_cast<std::uint32_t>(room[i]);
                if (low < 0xdc00 || low > 0xdfff) return false;
                c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
            } else if (c >= 0xdc00 && c <= 0xdfff) return false;
        } else if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
        bytes += c <= 0x7f ? 1 : c <= 0x7ff ? 2 : c <= 0xffff ? 3 : 4;
        if (bytes > RoomByteLimit) return false;
        if (c != L' ') nonSpace = true;
    }
    return nonSpace;
}
inline bool Field(std::wstring_view line, std::wstring_view key,
    bool english, std::wstring_view& value) {
    if (line.substr(0, key.size()) != key) return false;
    value = line.substr(key.size());
    // English Copy inserts one formatting space; any other room characters
    // belong to the ID and must survive unchanged (including surrounding spaces).
    if (english && !value.empty() && value.front() == L' ') value.remove_prefix(1);
    return !value.empty();
}
}

// Pure parsing: no clipboard, connection, identity or persistent-setting changes.
// Accept exactly the localized three-line Copy format, LF/CRLF separators and
// an optional final line ending. Unknown, repeated and additional fields fail.
inline std::optional<MeetingInvitation> ParseMeetingInvitation(std::wstring_view text) {
    using namespace MeetingInvitationDetail;
    if (text.empty() || text.size() > TextLimit) return std::nullopt;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<std::uint32_t>(text[i]);
        if (c == L'\r') {
            if (i + 1 == text.size() || text[i + 1] != L'\n') return std::nullopt;
        } else if (c != L'\n' && (c < 32 || (c >= 127 && c <= 159) || c == 0x2028 || c == 0x2029)) {
            return std::nullopt;
        }
    }
    if (text.back() == L'\n') {
        text.remove_suffix(1);
        if (!text.empty() && text.back() == L'\r') text.remove_suffix(1);
    }
    std::array<std::wstring_view, 3> lines{};
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto newline = text.find(L'\n');
        if ((i < 2) != (newline != text.npos)) return std::nullopt;
        lines[i] = text.substr(0, newline);
        if (!lines[i].empty() && lines[i].back() == L'\r') lines[i].remove_suffix(1);
        if (newline != text.npos) text.remove_prefix(newline + 1);
    }
    const bool english = lines[0] == L"LumaLive meeting";
    if (!english && lines[0] != L"LumaLive \u89c6\u9891\u4f1a\u8bae") return std::nullopt;
    std::wstring_view server, room;
    if (!Field(lines[1], english ? L"Server:" : L"\u670d\u52a1\u5668\uff1a", english, server) ||
        !Field(lines[2], english ? L"Meeting ID:" : L"\u4f1a\u8bae\u53f7\uff1a", english, room) ||
        !Server(server) || !Room(room)) return std::nullopt;
    return MeetingInvitation{std::wstring(server), std::wstring(room)};
}
}
