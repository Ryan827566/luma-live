#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IAgentRegistry {
public:
    virtual ~IAgentRegistry() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
