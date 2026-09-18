#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::tool_registry {
class IToolRegistry {
public:
    virtual ~IToolRegistry() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
