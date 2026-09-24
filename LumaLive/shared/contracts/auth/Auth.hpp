#pragma once
#include <cstdint>
#include <string>
namespace luma::contracts::auth {
struct UserProfile { std::string user_id; std::string username; std::string email; std::string display_name; };
struct AuthSession { std::string token; UserProfile user; std::int64_t expires_at_epoch_seconds{0}; };
}
