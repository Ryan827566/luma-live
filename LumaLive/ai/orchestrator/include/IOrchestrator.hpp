#pragma once
#include "AiTypes.hpp"
#include "IModelRouter.hpp"
#include <memory>
namespace luma::ai::orchestrator {
class IOrchestrator{public:virtual~IOrchestrator()=default;virtual core::AiResponse Execute(core::AiRequest)=0;};
class Orchestrator final:public IOrchestrator{std::shared_ptr<model_router::IModelRouter> router_;public:explicit Orchestrator(std::shared_ptr<model_router::IModelRouter>r):router_(std::move(r)){}core::AiResponse Execute(core::AiRequest)override;};
}