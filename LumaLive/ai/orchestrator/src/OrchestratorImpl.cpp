#include "IOrchestrator.hpp"
namespace luma::ai::orchestrator {
core::AiResponse Orchestrator::Execute(core::AiRequest r){core::AiResponse o;o.request_id=r.request_id;if(!router_){o.error="model router is unavailable";return o;}auto p=router_->Route(r.capability,r.metadata.contains("provider")?r.metadata.at("provider"):"");if(!p){o.error="no provider supports requested capability";return o;}return p->Execute(r);}
}