#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IModeratorAgent {
public:
    virtual ~IModeratorAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
