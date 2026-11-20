#include "MeetingInvitation.hpp"
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace luma::client::ui::preview;
namespace {
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::wstring English(std::wstring_view server, std::wstring_view room) {
    return L"LumaLive meeting\r\nServer: " + std::wstring(server) + L"\r\nMeeting ID: " + std::wstring(room);
}
std::wstring Chinese(std::wstring_view server, std::wstring_view room) {
    return L"LumaLive \u89c6\u9891\u4f1a\u8bae\r\n\u670d\u52a1\u5668\uff1a" + std::wstring(server) +
        L"\r\n\u4f1a\u8bae\u53f7\uff1a" + std::wstring(room);
}
void Accept(std::wstring_view text, std::wstring_view server, std::wstring_view room) {
    const auto result = ParseMeetingInvitation(text);
    Require(result.has_value(), "valid invitation rejected");
    Require(result->server == server && result->room == room, "invitation values changed");
}
void Reject(std::wstring_view text) {
    Require(!ParseMeetingInvitation(text), "invalid invitation accepted");
}
void CopiedText() {
    const auto room = L"\u4ea7\u54c1\u4f8b\u4f1a-42";
    for (const auto& text : {English(L"127.0.0.1:9000", room), Chinese(L"127.0.0.1:9000", room)}) {
        Accept(text, L"127.0.0.1:9000", room);
        Accept(text + L"\r\n", L"127.0.0.1:9000", room);
        auto lf = text;
        for (auto i = lf.find(L'\r'); i != lf.npos; i = lf.find(L'\r')) lf.erase(i, 1);
        Accept(lf, L"127.0.0.1:9000", room);
        Accept(lf + L"\n", L"127.0.0.1:9000", room);
    }
    Accept(English(L"localhost", L" room with spaces "), L"localhost", L" room with spaces ");
    Accept(Chinese(L"localhost", L" room with spaces "), L"localhost", L" room with spaces ");
    Accept(L"LumaLive meeting\nServer:localhost\nMeeting ID:demo", L"localhost", L"demo");
    // Returned fields own their memory; parsing does not depend on source lifetime.
    auto input = English(L"localhost", L"original");
    auto result = ParseMeetingInvitation(input);
    input.assign(L"overwritten");
    Require(result && result->server == L"localhost" && result->room == L"original", "parser retained a source view");
    std::cout << "PASS: Chinese/English Copy, CRLF/LF, optional terminator, exact fields and owned result\n";
}
void Servers() {
    for (const auto* server : {L"localhost", L"LOCALHOST:9000", L"meeting.example.invalid",
        L"meeting.example.invalid.:1", L"xn--bcher-kva.example:65535", L"192.0.2.1",
        L"255.255.255.255:65535", L"0.0.0.0:1", L"host:00001"}) {
        Accept(English(server, L"demo"), server, L"demo");
    }
    for (const auto* server : {L"", L" ", L":9000", L"host:", L"host:0", L"host:65536", L"host:999999999999999999",
        L"host:-1", L"host:+1", L"host: 9000", L"host:9000 ", L"host:9.0", L"host:9x", L"host:000001",
        L"host:9000:1", L"http://host:9000", L"https://host", L"tcp://host", L"user@host:9000",
        L"host/path", L"host\\path", L"host?query", L"host#fragment", L"has space", L" host", L"host ",
        L"host_name", L"-host", L"host-", L".host", L"host..example", L"host..", L"%68ost",
        L"256.0.0.1", L"01.2.3.4", L"1.2.3", L"1.2.3.4.5", L"1.2.3.", L"1234", L"1..2.3",
        L"[::1]", L"[::1]:9000", L"::1", L"2001:db8::1:9000", L"[fe80::1%3]:9000",
        L"\u670d\u52a1.example", L"host:\uff19\uff10\uff10\uff10"}) {
        Reject(English(server, L"demo"));
    }
    const auto label = std::wstring(63, L'a');
    Accept(English(label, L"demo"), label, L"demo");
    Reject(English(label + L"a", L"demo"));
    const auto host = label + L"." + label + L"." + label + L"." + std::wstring(61, L'b');
    Require(host.size() == 253, "host boundary fixture invalid");
    Accept(English(host, L"demo"), host, L"demo");
    Accept(English(host + L".:65535", L"demo"), host + L".:65535", L"demo");
    Reject(English(host + L"b", L"demo"));
    Reject(English(host + L"b.", L"demo"));
    std::cout << "PASS: DNS/IPv4, default port, numeric port and host boundaries; unsupported IPv6 rejected\n";
}
void Rooms() {
    for (const auto& room : {std::wstring(128, L'a'), std::wstring(64, L'\u00e9'),
        std::wstring(42, L'\u4f1a') + L"ab"}) Accept(English(L"localhost", room), L"localhost", room);
    for (const auto& room : {std::wstring(), std::wstring(L"   "), std::wstring(129, L'a'),
        std::wstring(65, L'\u00e9'), std::wstring(43, L'\u4f1a'), std::wstring(42, L'\u4f1a') + L"abc"}) {
        Reject(English(L"localhost", room));
    }
    std::wstring emoji;
    for (int i = 0; i < 32; ++i) emoji += L"\U0001f680";
    Accept(English(L"localhost", emoji), L"localhost", emoji); // 128 UTF-8 bytes.
    Reject(English(L"localhost", emoji + L"a"));
    for (const auto bad : {wchar_t(0xd800), wchar_t(0xdbff), wchar_t(0xdc00), wchar_t(0xdfff)}) {
        Reject(English(L"localhost", std::wstring(1, bad)));
        Reject(English(L"localhost", std::wstring(1, bad) + L"a"));
    }
    if constexpr (sizeof(wchar_t) > 2) Reject(English(L"localhost", std::wstring(1, wchar_t(0x110000))));
    std::cout << "PASS: UTF-8 byte limits and valid Unicode scalar values\n";
}
void MalformedText() {
    for (const auto* text : {L"", L"LumaLive meeting", L"Server: localhost\nMeeting ID: demo",
        L"Other meeting\nServer: localhost\nMeeting ID: demo",
        L"lumalive meeting\nServer: localhost\nMeeting ID: demo",
        L" LumaLive meeting\nServer: localhost\nMeeting ID: demo",
        L"LumaLive meeting\nMeeting ID: demo\nServer: localhost",
        L"LumaLive meeting\nServer: localhost\nServer: other",
        L"LumaLive meeting\nMeeting ID: demo\nMeeting ID: other",
        L"LumaLive meeting\nServer: localhost\nRoom: demo",
        L"LumaLive meeting\nserver: localhost\nMeeting ID: demo",
        L"LumaLive meeting\nServer: localhost\nMeeting ID:",
        L"LumaLive meeting\nServer:\nMeeting ID: demo",
        L"LumaLive meeting\n\nServer: localhost\nMeeting ID: demo",
        L"LumaLive meeting\rServer: localhost\rMeeting ID: demo",
        L"LumaLive meeting\r\r\nServer: localhost\nMeeting ID: demo",
        L"LumaLive meeting\n\u670d\u52a1\u5668\uff1alocalhost\n\u4f1a\u8bae\u53f7\uff1ademo",
        L"LumaLive \u89c6\u9891\u4f1a\u8bae\nServer: localhost\nMeeting ID: demo"}) Reject(text);
    const auto valid = English(L"localhost", L"demo");
    for (const auto* suffix : {L"\r", L"\n\n", L"\r\n\r\n", L"\nExtra: ignored",
        L"\nServer: other", L"\nMeeting ID: other", L"\nDisplay name: injected"}) Reject(valid + suffix);
    Reject(L"\n" + valid);
    Reject(std::wstring(4097, L'x'));
    Reject(valid + std::wstring(4096 - valid.size(), L' '));
    for (unsigned control = 0; control <= 159; ++control) {
        if (control >= 32 && control < 127) continue;
        const std::wstring bad(1, static_cast<wchar_t>(control));
        Reject(English(L"local" + bad + L"host", L"demo"));
        Reject(English(L"localhost", L"de" + bad + L"mo"));
        if (control != L'\n') Reject(valid + bad); // A single final LF is supported.
    }
    for (const auto separator : {wchar_t(0x2028), wchar_t(0x2029)})
        Reject(English(L"localhost", L"de" + std::wstring(1, separator) + L"mo"));
    // A NUL in a view is data, not permission to ignore the suffix.
    auto nul = valid; nul.push_back(L'\0'); nul += L"\nServer: ignored";
    Reject(nul);
    std::cout << "PASS: missing/duplicate/unknown keys, extra lines, oversized text, controls and embedded NUL\n";
}
}
int main() {
    try {
        CopiedText();
        Servers();
        Rooms();
        MalformedText();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
