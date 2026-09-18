#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::connect {
class IConnectService {
public:
    virtual ~IConnectService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
