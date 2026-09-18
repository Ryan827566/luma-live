#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IEffectAgent {
public:
    virtual ~IEffectAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
