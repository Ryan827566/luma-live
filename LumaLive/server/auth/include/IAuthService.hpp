#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "contracts/errors/Error.hpp"

namespace luma::server::auth {
class IAuthStore;
class ISmsProvider;
class IEmailProvider;
class IAuthService {
public:
    virtual ~IAuthService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result StartOnPort(std::uint16_t port) = 0;
    virtual shared::contracts::Result Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual std::uint16_t Port() const = 0;
    virtual shared::contracts::Result ConfigureDatabase(std::string connection_string) = 0;
};
std::unique_ptr<IAuthService> CreateAuthService();
std::unique_ptr<IAuthService> CreateAuthService(std::unique_ptr<IAuthStore> store);
// Explicit delivery adapters allow deployment composition and isolated end-to-end tests.
std::unique_ptr<IAuthService> CreateAuthService(
    std::unique_ptr<IAuthStore> store,std::unique_ptr<IEmailProvider> email,
    std::unique_ptr<ISmsProvider> sms);
// The supplied clock returns UTC epoch seconds and must be safe to call concurrently.
std::unique_ptr<IAuthService> CreateAuthService(
    std::unique_ptr<IAuthStore> store,std::function<std::int64_t()> clock);
}
