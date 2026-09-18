#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::agents {
class IEditorAgent {
public:
    virtual ~IEditorAgent()=default;
    virtual shared::contracts::Result Execute()=0;
};
}
