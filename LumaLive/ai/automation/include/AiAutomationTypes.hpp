#pragma once
#include <map>
#include <string>
#include <vector>

namespace luma::ai::automation {

enum class TriggerType { Manual, Event, Schedule, Condition };

struct Trigger {
    TriggerType type{TriggerType::Manual};
    std::string name;
    std::map<std::string, std::string> parameters;
};

struct ToolCall {
    std::string tool;
    std::map<std::string, std::string> arguments;
};

struct WorkflowStep {
    std::string id;
    std::string name;
    std::vector<ToolCall> actions;
};

struct WorkflowDefinition {
    std::string id;
    std::string name;
    std::string description;
    Trigger trigger;
    std::vector<WorkflowStep> steps;
};

struct WorkflowExecution {
    std::string execution_id;
    std::string workflow_id;
    std::string status;
    std::string error;
    std::string failed_step_id;
    std::vector<std::string> step_outputs;
    std::size_t completed_steps{0};
};

} // namespace luma::ai::automation
