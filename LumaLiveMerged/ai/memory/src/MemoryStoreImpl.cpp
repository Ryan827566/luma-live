#include "../include/IMemoryStore.hpp"
namespace luma::ai::memory {
class MemoryStoreImpl final : public IMemoryStore {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
