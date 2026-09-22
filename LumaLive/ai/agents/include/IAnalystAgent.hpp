#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IAnalystAgent {
public:
    virtual ~IAnalystAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
