#include "../include/IObjectStorage.hpp"
namespace luma::adapters::storage {
class ObjectStorageImpl final : public IObjectStorage {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
