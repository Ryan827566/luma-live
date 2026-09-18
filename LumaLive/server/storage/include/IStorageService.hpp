#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::storage {
class IStorageService {
public:
    virtual ~IStorageService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
