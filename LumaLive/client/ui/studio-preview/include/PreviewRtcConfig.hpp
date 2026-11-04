#pragma once
#include "PeerConnectionConfig.hpp"
#include <cstdlib>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include "PreviewNetworkSettings.hpp"
#endif

namespace luma::client::ui::preview {
// Deployment defaults shared by calls and meetings. Read once per Start; each
// session keeps its own snapshot for reconnects. Explicit configs bypass this.
// The optional path is for isolated tools/tests, never a process-wide override.
inline contracts::PeerConnectionConfig LoadPreviewRtcConfig(std::wstring* error = nullptr,
    const std::filesystem::path& custom = {}) {
    if (error) error->clear();
    contracts::PeerConnectionConfig config;
    const char* stun = std::getenv("LUMALIVE_STUN_SERVER");
    config.stun_servers.push_back(stun && *stun ? stun : "stun:stun.l.google.com:19302");
    const char* turn = std::getenv("LUMALIVE_TURN_URL");
    const char* user = std::getenv("LUMALIVE_TURN_USERNAME");
    const char* password = std::getenv("LUMALIVE_TURN_PASSWORD");
    if (turn && *turn) {
        config.turn_url = turn;
        config.turn_username = user ? user : "";
        config.turn_password = password ? password : "";
    }
#ifdef _WIN32
    PreviewNetworkSettingsDetail::Scratch saved;
    std::wstring loadError;
    if (!LoadPreviewNetworkSettings(saved.value, loadError, custom)) {
        if (error) *error = std::move(loadError);
        return config;
    }
    if (saved.value.enabled) {
        contracts::PeerConnectionConfig selected;
        if (!saved.value.stunUrl.empty()) selected.stun_servers.push_back(UiUtf8(saved.value.stunUrl.c_str()));
        selected.turn_url = UiUtf8(saved.value.turnUrl.c_str());
        selected.turn_username = UiUtf8(saved.value.turnUsername.c_str());
        selected.turn_password = UiUtf8(saved.value.turnPassword.c_str());
        return selected;
    }
#else
    (void)custom;
#endif
    return config;
}
}
