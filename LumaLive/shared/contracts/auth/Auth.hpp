#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace luma::contracts::auth {

struct UserProfile {
    std::string user_id;
    std::string username;
    std::string email;
    std::string display_name;
    std::string avatar_url;
    std::string phone;
};

struct AuthSession {
    std::string token;
    std::string refresh_token;
    std::string session_id;
    std::string device_id;
    std::string device_name;
    UserProfile user;
    std::int64_t expires_at_epoch_seconds{0};
    std::int64_t refresh_expires_at_epoch_seconds{0};
};

struct SecuritySummary {
    bool email_verified{false};
    bool phone_verified{false};
    bool mfa_enabled{false};
    std::uint32_t failed_login_attempts{0};
    std::uint32_t active_session_count{0};
};

struct DeviceSession {
    std::string session_id;
    std::string device_id;
    std::string device_name;
    std::string remote_address;
    std::int64_t created_at_epoch_seconds{0};
    std::int64_t last_seen_epoch_seconds{0};
    bool current{false};
};

struct SecurityEvent {
    std::string event_id;
    std::string type;
    std::string detail;
    std::int64_t created_at_epoch_seconds{0};
};

}
