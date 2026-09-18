#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::domain::effects {
class IEffectsService {
public:
    virtual ~IEffectsService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
