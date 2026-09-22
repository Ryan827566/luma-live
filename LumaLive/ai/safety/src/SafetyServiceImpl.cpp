#include "../include/ISafetyService.hpp"
namespace luma::ai::safety {
class SafetyServiceImpl final : public ISafetyService {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
