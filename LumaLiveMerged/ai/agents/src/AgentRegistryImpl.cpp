#include "../include/IAgentRegistry.hpp"
namespace luma::ai::agents {
class AgentRegistryImpl final : public IAgentRegistry {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
