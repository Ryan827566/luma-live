#include "../include/IApiGateway.hpp"
namespace luma::server::api_gateway {
class ApiGatewayImpl final : public IApiGateway {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
