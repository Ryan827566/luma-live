#include "../include/ISignalingService.hpp"
namespace luma::server::signaling {
class SignalingServiceImpl final : public ISignalingService {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
