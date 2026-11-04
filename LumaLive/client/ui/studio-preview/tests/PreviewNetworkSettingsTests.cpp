#include "PreviewNetworkSettings.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace luma::client::ui::preview;
namespace {
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
PreviewNetworkSettings Fixture() {
    return {true, L"stun:stun.example.invalid:3478", L"turns:relay.example.invalid:5349?transport=tcp",
        L"fixture-user-\u7528\u6237", L"fixture-password-\u5bc6\u7801-\U0001F511"};
}
bool Same(const PreviewNetworkSettings& a, const PreviewNetworkSettings& b) {
    return a.enabled == b.enabled && a.stunUrl == b.stunUrl && a.turnUrl == b.turnUrl &&
        a.turnUsername == b.turnUsername && a.turnPassword == b.turnPassword;
}
void Validation() {
    std::wstring error;
    Require(ValidatePreviewNetworkSettings({}, error) && error.empty(), "empty disabled settings were rejected");
    PreviewNetworkSettings direct; direct.enabled = true;
    Require(ValidatePreviewNetworkSettings(direct, error), "direct-only settings were rejected");
    for (const auto* url : {L"stun:localhost", L"stun:192.0.2.1",
        L"stun:[2001:db8::1]:3478", L"stun:[::1]", L"stun:[::ffff:192.0.2.1]",
        L"stun:[1:2:3:4:5:6:7:8]", L"stun:xn--bcher-kva.example.", L"stun:HOST.example:65535"}) {
        auto value = direct; value.stunUrl = url;
        Require(ValidatePreviewNetworkSettings(value, error), "valid STUN URI was rejected");
    }
    for (const auto* url : {L"turn:relay.example.invalid", L"turn:192.0.2.3:3478?transport=udp",
        L"turn:[2001:db8::2]:3478?transport=tcp", L"turns:relay.example.invalid:5349",
        L"turns:[::1]?transport=tcp"}) {
        auto value = direct; value.turnUrl = url;
        Require(ValidatePreviewNetworkSettings(value, error), "valid TURN URI was rejected");
    }
    for (const auto* url : {L"stuns:stun.example.invalid:5349", L"https://stun.example.invalid", L"stun://stun.example.invalid", L"stun:",
        L"stun:host/path", L"stun:user:pass@host", L"stun:host#fragment", L"stun:host?transport=udp",
        L"stun:host:", L"stun:host:0", L"stun:host:65536", L"stun:host:-1", L"stun:host:abcd",
        L"stun:host:3478:1", L" stun:host", L"stun:has space", L"stun:host\t", L"stun:host\\path",
        L"stun:256.0.0.1", L"stun:01.2.3.4", L"stun:1.2.3", L"stun:.host", L"stun:host..example",
        L"stun:-host.example", L"stun:host-.example", L"stun:host_name", L"stun:%68ost",
        L"stun:[garbage]", L"stun:[1:2:3]", L"stun:[1:2:3:4:5:6:7:8:9]", L"stun:[1::2::3]",
        L"stun:[1:2:3:4:5:6:7:8::]", L"stun:[:::1]", L"stun:[::1:]", L"stun:[fe80::1%3]",
        L"stun:[::ffff:192.0.2.999]", L"stun:[::ffff:192.0.2.1:2]", L"stun:2001:db8::1",
        L"stun:[::1]extra", L"stun:[::1", L"stun:\u670d\u52a1.example"}) {
        auto value = direct; value.stunUrl = url;
        Require(!ValidatePreviewNetworkSettings(value, error) && !error.empty(), "invalid STUN URI was accepted");
    }
    for (const auto* url : {L"stun:relay.example.invalid", L"turn://relay.example.invalid", L"turn:host?transport=sctp",
        L"turn:host?transport=udp&secret=value", L"turn:host?transport=udp?x=y", L"turn:host?key=value",
        L"turns:host?transport=udp", L"turn:host#x", L"turn:user:secret@host", L"turn:host/"}) {
        auto value = direct; value.turnUrl = url;
        Require(!ValidatePreviewNetworkSettings(value, error), "invalid TURN URI was accepted");
    }
    auto value = Fixture();
    value.turnPassword.clear(); Require(!ValidatePreviewNetworkSettings(value, error), "username-only credential accepted");
    value = Fixture(); value.turnUsername.clear(); Require(!ValidatePreviewNetworkSettings(value, error), "password-only credential accepted");
    value = Fixture(); value.turnUrl.clear(); Require(!ValidatePreviewNetworkSettings(value, error), "credentials without TURN URI accepted");
    value = Fixture(); value.turnPassword = L"line\nbreak"; Require(!ValidatePreviewNetworkSettings(value, error), "newline in credential accepted");
    value = Fixture(); value.turnPassword.assign(1, L'\0'); Require(!ValidatePreviewNetworkSettings(value, error), "embedded NUL accepted");
    value = Fixture(); value.turnPassword.assign(1, wchar_t(0xd800)); Require(!ValidatePreviewNetworkSettings(value, error), "unpaired surrogate accepted");
    value = Fixture(); value.turnUsername.assign(1025, L'a'); Require(!ValidatePreviewNetworkSettings(value, error), "oversized username accepted");
    value = Fixture(); value.turnPassword.assign(4097, L'a'); Require(!ValidatePreviewNetworkSettings(value, error), "oversized password accepted");
    value = Fixture(); value.stunUrl.assign(2049, L'a'); Require(!ValidatePreviewNetworkSettings(value, error), "oversized URI accepted");
    Require(error.find(L"fixture-password") == error.npos, "validation disclosed a credential");
    error = L"stale"; Require(ValidatePreviewNetworkSettings(Fixture(), error) && error.empty(), "validation retained stale error");
    std::cout << "PASS: URI, Unicode, credential pairing and size validation\n";
}

#ifdef _WIN32
struct PrivateDirectory {
    std::filesystem::path path;
    PrivateDirectory() {
        wchar_t temporary[32768]{}, id[40]{};
        const DWORD count = GetTempPathW(32768, temporary);
        GUID guid{};
        Require(count && count < 32768 && SUCCEEDED(CoCreateGuid(&guid)) && StringFromGUID2(guid, id, 40), "private test path unavailable");
        path = std::filesystem::path(temporary) / (std::wstring(L"luma-network-settings-test-") + id);
        Require(std::filesystem::create_directory(path), "private test directory creation failed");
    }
    ~PrivateDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
std::string Read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    Require(bool(input), "private fixture read failed");
    return std::string(std::istreambuf_iterator<char>(input), {});
}
void Write(const std::filesystem::path& file, const std::string& bytes) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Require(bool(output), "private fixture write failed");
}
void WriteProtected(const std::filesystem::path& file, const std::string& plain) {
    DATA_BLOB source{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    PreviewNetworkSettingsDetail::Blob encrypted;
    Require(CryptProtectData(&source, L"test only", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted.value) != FALSE, "test DPAPI encryption failed");
    Write(file, std::string(reinterpret_cast<char*>(encrypted.value.pbData), encrypted.value.cbData));
}
void Persistence() {
    PrivateDirectory directory;
    const auto file = directory.path / L"rtc-network.dat";
    const auto corrupt = directory.path / L"corrupt.dat";
    std::wstring error;
    auto loaded = Fixture();
    Require(LoadPreviewNetworkSettings(loaded, error, file) && Same(loaded, {}) && error.empty(), "missing private file did not yield disabled defaults");
    Require(!std::filesystem::exists(file), "load unexpectedly created a file");
    const auto saved = Fixture();
    Require(SavePreviewNetworkSettings(saved, error, file) && error.empty(), "DPAPI save failed");
    Require(LoadPreviewNetworkSettings(loaded, error, file) && Same(loaded, saved), "DPAPI UTF-8 roundtrip failed");
    const auto original = Read(file);
    for (const auto* secret : {"fixture-user", "fixture-password", "stun.example.invalid", "relay.example.invalid", "LumaLiveRtc1"})
        Require(original.find(secret) == original.npos, "encrypted file exposed settings plaintext");
    auto invalid = saved; invalid.turnPassword = L"bad\npassword";
    Require(!SavePreviewNetworkSettings(invalid, error, file) && Read(file) == original, "invalid save changed existing file");
    {
        // Denying delete sharing makes the final atomic replacement fail after
        // the new encrypted temporary file has already been written/flushed.
        PreviewNetworkSettingsDetail::File lock{CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        Require(lock.value != INVALID_HANDLE_VALUE, "atomic-save test lock failed");
        auto changed = saved; changed.turnPassword = L"replacement-fixture-password";
        Require(!SavePreviewNetworkSettings(changed, error, file) && !error.empty(), "locked destination save unexpectedly succeeded");
    }
    Require(Read(file) == original, "failed atomic save changed previous settings");
    for (const auto& entry : std::filesystem::directory_iterator(directory.path))
        Require(entry.path().extension() != L".tmp", "failed save left temporary ciphertext behind");
    Require(LoadPreviewNetworkSettings(loaded, error, file) && Same(loaded, saved), "old settings unreadable after failed save");
    auto disabled = saved; disabled.enabled = false;
    Require(SavePreviewNetworkSettings(disabled, error, file) && LoadPreviewNetworkSettings(loaded, error, file) && Same(loaded, disabled), "disabled snapshot failed to roundtrip");
    PreviewNetworkSettings direct; direct.enabled = true;
    Require(SavePreviewNetworkSettings(direct, error, file) && LoadPreviewNetworkSettings(loaded, error, file) && Same(loaded, direct), "direct-only snapshot failed to roundtrip");
    const auto checkCorrupt = [&] {
        const auto before = Read(corrupt);
        loaded = saved;
        Require(!LoadPreviewNetworkSettings(loaded, error, corrupt) && Same(loaded, {}) && !error.empty(), "corrupt file accepted or partial credentials retained");
        Require(error.find(L"fixture") == error.npos && Read(corrupt) == before, "corrupt-file read disclosed data or changed disk");
    };
    Write(corrupt, "broken"); checkCorrupt();
    Write(corrupt, ""); checkCorrupt();
    Write(corrupt, std::string(65537, 'x')); checkCorrupt();
    auto tampered = original; tampered[tampered.size() / 2] ^= 0x55;
    Write(corrupt, tampered); checkCorrupt();
    // Valid DPAPI envelopes with malformed application payload must also fail.
    WriteProtected(corrupt, "wrong-format"); checkCorrupt();
    PreviewNetworkSettingsDetail::Plaintext plain;
    Require(PreviewNetworkSettingsDetail::Encode(saved, plain), "private payload setup failed");
    auto bad = plain.value; bad.push_back('x'); WriteProtected(corrupt, bad); checkCorrupt();
    bad = plain.value; bad.pop_back(); WriteProtected(corrupt, bad); checkCorrupt();
    bad = plain.value; bad[sizeof(PreviewNetworkSettingsDetail::Magic)] = 2; WriteProtected(corrupt, bad); checkCorrupt();
    bad = plain.value; bad[sizeof(PreviewNetworkSettingsDetail::Magic) + 5] = char(0xff); WriteProtected(corrupt, bad); checkCorrupt();
    auto invalidUri = saved; invalidUri.turnUrl = L"turn:user:password@host";
    PreviewNetworkSettingsDetail::Plaintext invalidPlain;
    Require(PreviewNetworkSettingsDetail::Encode(invalidUri, invalidPlain), "invalid URI payload setup failed");
    WriteProtected(corrupt, invalidPlain.value); checkCorrupt();
    Require(!SavePreviewNetworkSettings(saved, error, directory.path) && !error.empty(), "directory destination unexpectedly saved");
    std::cout << "PASS: real current-user DPAPI, whole-content encryption, atomic failure retention, corruption and private paths\n";
}
#endif
}
int main() {
    try {
        Validation();
#ifdef _WIN32
        Persistence();
#endif
        return 0;
    } catch (const std::exception& error) {
        // Assertions are constant messages; no credentials or user paths.
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
