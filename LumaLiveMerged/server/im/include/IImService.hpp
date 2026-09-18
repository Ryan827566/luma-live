#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::im {
class IImService {
public:
    virtual ~IImService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
