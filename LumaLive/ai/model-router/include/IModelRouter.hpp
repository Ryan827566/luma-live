#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::model_router {
class IModelRouter {
public:
    virtual ~IModelRouter() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
