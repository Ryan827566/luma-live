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
        output = "ok";
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
    assert(record->calls == 2);

    workflow.steps[1].actions[0].tool = "missing";
    const auto failed = engine.Execute(workflow, "exec-2");
    assert(failed.status == "failed");
    assert(failed.completed_steps == 1);
    return 0;
}
