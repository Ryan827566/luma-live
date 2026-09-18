#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::adapters::model {
class IModelAdapter {
public:
    virtual ~IModelAdapter() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
