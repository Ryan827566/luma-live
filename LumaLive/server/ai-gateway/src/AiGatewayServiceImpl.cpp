#include "../include/IAiGatewayService.hpp"
namespace luma::server::ai_gateway {
class AiGatewayServiceImpl final : public IAiGatewayService {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
