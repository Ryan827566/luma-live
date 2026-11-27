#include <cassert>
#include <memory>
#include <string>
#include "AiWorkflowEngine.hpp"

using namespace luma::ai::automation;

class RecordingTool final : public IAiTool {
public:
    explicit RecordingTool(std::string name, bool succeed = true)
        : name_(std::move(name)), succeed_(succeed) {}
    const std::string& Name() const override { return name_; }
    bool Execute(const ToolCall&, std::string& output) override {
        ++calls;
        output = "ok-" + std::to_string(calls);
        return succeed_;
    }
    int calls{0};
private:
    std::string name_;
    bool succeed_;
};

int main() {
    AiToolRegistry registry;
    auto record = std::make_shared<RecordingTool>("send_message");
    assert(registry.Register(record));

    WorkflowDefinition workflow;
    workflow.id = "meeting-follow-up";
    workflow.name = "Meeting follow-up";
    workflow.trigger = {TriggerType::Event, "meeting_ended", {}};
    workflow.steps = {
        {"summary", "Generate summary", {{"send_message", {{"text", "summary"}}}}},
        {"todo", "Send tasks", {{"send_message", {{"text", "todo"}}}}}
    };

    AiWorkflowEngine engine(registry);
    const auto result = engine.Execute(workflow, "exec-1");
    assert(result.status == "succeeded");
    assert(result.completed_steps == 2);
    assert(result.step_outputs.size() == 2);
    assert(result.step_outputs[0] == "ok-1");
    assert(result.step_outputs[1] == "ok-2");
    assert(record->calls == 2);

    workflow.steps[1].actions[0].tool = "missing";
    const auto missing_tool = engine.Execute(workflow, "exec-2");
    assert(missing_tool.status == "failed");
    assert(missing_tool.completed_steps == 1);
    assert(missing_tool.failed_step_id == "todo");
    assert(missing_tool.error == "tool not registered: missing");

    WorkflowDefinition empty_id = workflow;
    empty_id.id.clear();
    const auto empty_id_result = engine.Execute(empty_id, "exec-3");
    assert(empty_id_result.status == "failed");
    assert(empty_id_result.error == "workflow id is empty");

    WorkflowDefinition empty_steps = workflow;
    empty_steps.id = "empty";
    empty_steps.steps.clear();
    const auto empty_result = engine.Execute(empty_steps, "exec-4");
    assert(empty_result.status == "failed");
    assert(empty_result.error == "workflow has no steps");

    WorkflowDefinition duplicate_steps = workflow;
    duplicate_steps.id = "duplicate";
    duplicate_steps.steps[1].id = duplicate_steps.steps[0].id;
    const auto duplicate_result = engine.Execute(duplicate_steps, "exec-5");
    assert(duplicate_result.status == "failed");
    assert(duplicate_result.error == "duplicate workflow step id: summary");

    WorkflowDefinition no_step_id = workflow;
    no_step_id.id = "no-step-id";
    no_step_id.steps[0].id.clear();
    const auto no_step_id_result = engine.Execute(no_step_id, "exec-6");
    assert(no_step_id_result.status == "failed");
    assert(no_step_id_result.error == "workflow step id is empty");

    WorkflowDefinition no_actions = workflow;
    no_actions.id = "no-actions";
    no_actions.steps[0].actions.clear();
    const auto no_actions_result = engine.Execute(no_actions, "exec-7");
    assert(no_actions_result.status == "failed");
    assert(no_actions_result.error == "workflow step has no actions: summary");

    WorkflowDefinition empty_tool = workflow;
    empty_tool.id = "empty-tool";
    empty_tool.steps[0].actions[0].tool.clear();
    const auto empty_tool_result = engine.Execute(empty_tool, "exec-8");
    assert(empty_tool_result.status == "failed");
    assert(empty_tool_result.failed_step_id == "summary");
    assert(empty_tool_result.error == "tool name is empty");

    AiToolRegistry failing_registry;
    auto failing = std::make_shared<RecordingTool>("fail", false);
    assert(failing_registry.Register(failing));
    AiWorkflowEngine failing_engine(failing_registry);
    WorkflowDefinition failing_workflow;
    failing_workflow.id = "failure";
    failing_workflow.steps = {{"step-1", "Failing step", {{"fail", {}}}}};
    const auto execution_failure = failing_engine.Execute(failing_workflow, "exec-9");
    assert(execution_failure.status == "failed");
    assert(execution_failure.completed_steps == 0);
    assert(execution_failure.failed_step_id == "step-1");
    assert(execution_failure.error == "tool execution failed: fail");
    return 0;
}
