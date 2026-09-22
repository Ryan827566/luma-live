#include "../include/IToolRegistry.hpp"
namespace luma::ai::tool_registry {
class ToolRegistryImpl final : public IToolRegistry {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
