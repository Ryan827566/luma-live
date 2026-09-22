#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::memory {
class IMemoryStore {
public:
    virtual ~IMemoryStore() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
