#include "../include/IModelRouter.hpp"
namespace luma::ai::model_router {
class ModelRouterImpl final : public IModelRouter {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
