#pragma once
#include "PeerConnectionConfig.hpp"
#include <cstdlib>

namespace luma::client::ui::preview {
// Deployment defaults shared by calls and meetings. Read once per Start; each
// session keeps its own snapshot for reconnects. Explicit configs bypass this.
inline contracts::PeerConnectionConfig LoadPreviewRtcConfig() {
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
    return config;
}
}
