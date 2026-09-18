#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::api_gateway {
class IApiGateway {
public:
    virtual ~IApiGateway() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
