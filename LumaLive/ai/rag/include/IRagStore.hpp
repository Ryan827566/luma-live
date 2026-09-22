#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::rag {
class IRagStore {
public:
    virtual ~IRagStore() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
