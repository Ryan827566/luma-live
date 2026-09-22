#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::adapters::storage {
class IObjectStorage {
public:
    virtual ~IObjectStorage() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
