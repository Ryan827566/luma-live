#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::ai_gateway {
class IAiGatewayService {
public:
    virtual ~IAiGatewayService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
