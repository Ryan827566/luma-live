#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IProducerAgent {
public:
    virtual ~IProducerAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
