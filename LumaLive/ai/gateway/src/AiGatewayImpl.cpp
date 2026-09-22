#include "../include/IAiGateway.hpp"
namespace luma::ai::gateway {
class AiGatewayImpl final : public IAiGateway {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
