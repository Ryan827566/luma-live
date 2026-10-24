#include "AiWorkflowEngine.hpp"

#include <set>

namespace luma::ai::automation {

AiWorkflowEngine::AiWorkflowEngine(AiToolRegistry& registry) : registry_(registry) {}

WorkflowExecution AiWorkflowEngine::Execute(const WorkflowDefinition& workflow,
                                            const std::string& execution_id) {
    WorkflowExecution result{execution_id.empty() ? workflow.id + "-execution" : execution_id,
                             workflow.id, "running", {}, {}, {}, 0};

    if (workflow.id.empty()) {
        result.status = "failed";
        result.error = "workflow id is empty";
        return result;
    }
    if (workflow.steps.empty()) {
        result.status = "failed";
        result.error = "workflow has no steps";
        return result;
    }

    std::set<std::string> step_ids;
    for (const auto& step : workflow.steps) {
        if (step.id.empty()) {
            result.status = "failed";
            result.error = "workflow step id is empty";
            result.failed_step_id = step.id;
            return result;
        }
        if (!step_ids.insert(step.id).second) {
            result.status = "failed";
            result.error = "duplicate workflow step id: " + step.id;
            result.failed_step_id = step.id;
            return result;
        }
        if (step.actions.empty()) {
            result.status = "failed";
            result.error = "workflow step has no actions: " + step.id;
            result.failed_step_id = step.id;
            return result;
        }
    }

    for (const auto& step : workflow.steps) {
        for (const auto& call : step.actions) {
            if (call.tool.empty()) {
                result.status = "failed";
                result.error = "tool name is empty";
                result.failed_step_id = step.id;
                return result;
            }

            auto tool = registry_.Find(call.tool);
            if (!tool) {
                result.status = "failed";
                result.error = "tool not registered: " + call.tool;
                result.failed_step_id = step.id;
                return result;
            }

            std::string output;
            if (!tool->Execute(call, output)) {
                result.status = "failed";
                result.error = "tool execution failed: " + call.tool;
                result.failed_step_id = step.id;
                return result;
            }
            result.step_outputs.push_back(output);
        }
        ++result.completed_steps;
    }

    result.status = "succeeded";
    return result;
}

} // namespace luma::ai::automation
