#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::platform {
class IPlatformService {
public:
    virtual ~IPlatformService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
