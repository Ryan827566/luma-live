#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::adapters::database {
class IDatabase {
public:
    virtual ~IDatabase() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
