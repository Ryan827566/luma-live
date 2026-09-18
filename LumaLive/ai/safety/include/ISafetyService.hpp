#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::safety {
class ISafetyService {
public:
    virtual ~ISafetyService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
