#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::auth {
class IAuthService {
public:
    virtual ~IAuthService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
