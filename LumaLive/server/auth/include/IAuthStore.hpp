#pragma once
#include "contracts/errors/Error.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace luma::server::auth {

struct AuthUserRecord {
    std::string id;
    std::string username;
    std::string email;
    std::string display_name;
    std::string avatar_url;
    std::string phone;
    std::string salt_hex;
    std::string verifier_hex;
    bool email_verified{false};
    bool phone_verified{false};
    bool mfa_enabled{false};
    std::string mfa_recovery_hash;
    std::string email_verify_hash;
    std::int64_t email_verify_expires{0};
    std::string reset_token_hash;
    std::int64_t reset_token_expires{0};
};

struct AuthSecurityEventRecord {
    std::string event_id;
    std::string user_id;
    std::string type;
    std::string detail;
    std::int64_t created_at_epoch_seconds{0};
};

class IAuthStore {
public:
    virtual ~IAuthStore() = default;

    virtual shared::contracts::Result Open() = 0;
    virtual void Close() noexcept = 0;

    virtual shared::contracts::Result LoadUsers(
        std::vector<AuthUserRecord>& users) = 0;

    // Atomic replacement of the server-side user snapshot.
    // The AuthService serializes mutations, so the repository can commit this
    // as one transaction and never expose a partially-written account set.
    virtual shared::contracts::Result ReplaceUsers(
        const std::vector<AuthUserRecord>& users) = 0;

    virtual shared::contracts::Result LoadSecurityEvents(
        std::vector<AuthSecurityEventRecord>& events) = 0;

    virtual shared::contracts::Result AppendSecurityEvent(
        const AuthSecurityEventRecord& event) = 0;
};

std::unique_ptr<IAuthStore> CreateInMemoryAuthStore();
std::unique_ptr<IAuthStore> CreatePostgresAuthStore(std::string connection_string);
std::unique_ptr<IAuthStore> CreateAuthStoreFromEnvironment();

}
