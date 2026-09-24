#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "contracts/auth/Auth.hpp"

namespace luma::client::account {
struct OperationResult { bool success{false}; std::string message; };
class IAccountService {
public:
    virtual ~IAccountService() = default;
    virtual OperationResult Start() = 0;
    virtual OperationResult Connect(std::string host, std::uint16_t port) = 0;
    virtual OperationResult Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual OperationResult Register(std::string username,std::string email,std::string display_name,std::string password) = 0;
    virtual OperationResult Login(std::string identifier,std::string password) = 0;
    virtual OperationResult Logout() = 0;
    virtual OperationResult ValidateSession() = 0;
    virtual bool IsAuthenticated() const = 0;
    virtual contracts::auth::AuthSession Session() const = 0;
    virtual OperationResult Execute(std::string_view operation) = 0;
};
std::unique_ptr<IAccountService> CreateAccountService();
}
