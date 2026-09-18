#include "../include/IModelAdapter.hpp"
namespace luma::adapters::model {
class ModelAdapterImpl final : public IModelAdapter {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
