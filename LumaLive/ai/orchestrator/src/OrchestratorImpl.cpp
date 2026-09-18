#include "../include/IOrchestrator.hpp"
namespace luma::ai::orchestrator {
class OrchestratorImpl final : public IOrchestrator {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
