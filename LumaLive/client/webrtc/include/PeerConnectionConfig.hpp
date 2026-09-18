#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace luma::contracts {
struct PeerConnectionConfig {
    std::vector<std::string> stun_servers;
    std::string turn_url;
    std::string turn_username;
    std::string turn_password;
    std::string value;
    std::int64_t sequence{0};
};
}
