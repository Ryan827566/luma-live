#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IVisionAgent {
public:
    virtual ~IVisionAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
