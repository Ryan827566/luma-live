#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::orchestrator {
class IOrchestrator {
public:
    virtual ~IOrchestrator() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
