#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::domain::studio {
class IStudioService {
public:
    virtual ~IStudioService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
