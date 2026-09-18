#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::application {
class IApplicationService {
public:
    virtual ~IApplicationService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
