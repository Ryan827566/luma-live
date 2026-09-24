#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "contracts/errors/Error.hpp"

namespace luma::server::auth {
class IAuthService {
public:
    virtual ~IAuthService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result StartOnPort(std::uint16_t port) = 0;
    virtual shared::contracts::Result Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual std::uint16_t Port() const = 0;
    virtual shared::contracts::Result ConfigureStore(std::string path) = 0;
};
std::unique_ptr<IAuthService> CreateAuthService();
}
