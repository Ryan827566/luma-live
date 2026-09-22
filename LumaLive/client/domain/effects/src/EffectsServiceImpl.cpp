#include "../include/IEffectsService.hpp"
namespace luma::client::domain::effects {
class EffectsServiceImpl final : public IEffectsService {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
