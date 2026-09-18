#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IChatAgent {
public:
    virtual ~IChatAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
