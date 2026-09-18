#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IHostAgent {
public:
    virtual ~IHostAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
