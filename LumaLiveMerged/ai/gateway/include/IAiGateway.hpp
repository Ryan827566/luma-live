#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::gateway {
class IAiGateway {
public:
    virtual ~IAiGateway() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
