#include "AiWorkflowEngine.hpp"

namespace luma::ai::automation {

AiWorkflowEngine::AiWorkflowEngine(AiToolRegistry& registry) : registry_(registry) {}

WorkflowExecution AiWorkflowEngine::Execute(const WorkflowDefinition& workflow,
                                            const std::string& execution_id) {
    WorkflowExecution result{execution_id.empty() ? workflow.id + "-execution" : execution_id,
                             workflow.id, "running", {}, 0};

    for (const auto& step : workflow.steps) {
        for (const auto& call : step.actions) {
            auto tool = registry_.Find(call.tool);
            if (!tool) {
                result.status = "failed";
                result.error = "tool not registered: " + call.tool;
                return result;
            }
            std::string output;
            if (!tool->Execute(call, output)) {
                result.status = "failed";
                result.error = "tool execution failed: " + call.tool;
                return result;
            }
        }
        ++result.completed_steps;
    }

    result.status = "succeeded";
    return result;
}

} // namespace luma::ai::automation
