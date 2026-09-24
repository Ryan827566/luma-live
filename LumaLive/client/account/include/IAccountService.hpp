#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "contracts/auth/Auth.hpp"

namespace luma::client::account {

struct OperationResult {
    bool success{false};
    std::string message;
};

class IAccountService {
public:
    virtual ~IAccountService() = default;
    virtual OperationResult Start() = 0;
    virtual OperationResult Connect(std::string host, std::uint16_t port) = 0;
    virtual OperationResult Stop() = 0;
    virtual bool IsRunning() const = 0;

    virtual OperationResult Register(std::string username,std::string email,std::string display_name,std::string password) = 0;
    virtual OperationResult Login(
        std::string identifier,
        std::string password,
        std::string device_id = {},
        std::string device_name = {},
        std::string mfa_code = {}) = 0;
    virtual OperationResult Logout() = 0;
    virtual OperationResult ValidateSession() = 0;

    virtual OperationResult GetProfile() = 0;
    virtual OperationResult UpdateProfile(std::string username,std::string email,std::string display_name,std::string avatar_url) = 0;
    virtual OperationResult DeleteAccount() = 0;

    virtual OperationResult RequestEmailVerification() = 0;
    virtual OperationResult VerifyEmail(std::string verification_token) = 0;
    virtual OperationResult RequestPhoneVerification(std::string phone) = 0;
    virtual OperationResult VerifyPhone(std::string challenge_id,std::string code) = 0;

    virtual OperationResult RequestPhoneLoginCode(std::string phone) = 0;
    virtual OperationResult LoginWithPhoneCode(
        std::string challenge_id,
        std::string code,
        std::string device_id = {},
        std::string device_name = {},
        std::string mfa_code = {}) = 0;
    virtual OperationResult ChangePassword(std::string current_password,std::string new_password) = 0;
    virtual OperationResult RequestPasswordReset(std::string identifier) = 0;
    virtual OperationResult ResetPassword(std::string reset_token,std::string new_password) = 0;

    virtual OperationResult GetSecuritySummary() = 0;
    virtual contracts::auth::SecuritySummary Security() const = 0;
    virtual OperationResult EnableMfa() = 0;
    virtual OperationResult DisableMfa(std::string recovery_code) = 0;

    virtual OperationResult GetSessions() = 0;
    virtual const std::vector<contracts::auth::DeviceSession>& Sessions() const = 0;
    virtual OperationResult RevokeSession(std::string session_id) = 0;
    virtual OperationResult RevokeOtherSessions() = 0;

    virtual OperationResult GetSecurityEvents() = 0;
    virtual const std::vector<contracts::auth::SecurityEvent>& SecurityEvents() const = 0;

    virtual bool IsAuthenticated() const = 0;
    virtual contracts::auth::AuthSession Session() const = 0;
    virtual OperationResult Execute(std::string_view operation) = 0;
};

std::unique_ptr<IAccountService> CreateAccountService();

}
