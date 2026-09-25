#include <cassert>
#include <memory>
#include "AiWorkflowEngine.hpp"

using namespace luma::ai::automation;

class RecordingTool final : public IAiTool {
public:
    explicit RecordingTool(std::string name) : name_(std::move(name)) {}
    const std::string& Name() const override { return name_; }
    bool Execute(const ToolCall&, std::string& output) override {
        ++calls;
        output = "ok-" + std::to_string(calls);
        return true;
    }
    int calls{0};
private:
    std::string name_;
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
    const auto failed = engine.Execute(workflow, "exec-2");
    assert(failed.status == "failed");
    assert(failed.completed_steps == 1);
    assert(failed.failed_step_id == "todo");
    assert(failed.error == "tool not registered: missing");

    WorkflowDefinition empty_steps = workflow;
    empty_steps.id = "empty";
    empty_steps.steps.clear();
    const auto empty_result = engine.Execute(empty_steps, "exec-3");
    assert(empty_result.status == "failed");
    assert(empty_result.error == "workflow has no steps");

    WorkflowDefinition duplicate_steps = workflow;
    duplicate_steps.id = "duplicate";
    duplicate_steps.steps[1].id = duplicate_steps.steps[0].id;
    const auto duplicate_result = engine.Execute(duplicate_steps, "exec-4");
    assert(duplicate_result.status == "failed");
    assert(duplicate_result.error == "duplicate workflow step id: summary");

    WorkflowDefinition no_actions = workflow;
    no_actions.id = "no-actions";
    no_actions.steps[0].actions.clear();
    const auto no_actions_result = engine.Execute(no_actions, "exec-5");
    assert(no_actions_result.status == "failed");
    assert(no_actions_result.error == "workflow step has no actions: summary");

    WorkflowDefinition empty_tool = workflow;
    empty_tool.id = "empty-tool";
    empty_tool.steps[0].actions[0].tool.clear();
    const auto empty_tool_result = engine.Execute(empty_tool, "exec-6");
    assert(empty_tool_result.status == "failed");
    assert(empty_tool_result.failed_step_id == "summary");
    assert(empty_tool_result.error == "tool name is empty");

    return 0;
}
