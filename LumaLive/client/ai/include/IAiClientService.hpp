#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::ai {
class IAiClientService {
public:
    virtual ~IAiClientService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
