#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IDirectorAgent {
public:
    virtual ~IDirectorAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
