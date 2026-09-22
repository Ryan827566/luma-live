#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class ITranslationAgent {
public:
    virtual ~ITranslationAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
