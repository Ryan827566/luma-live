#pragma once
#include <functional>
#include "AiAutomationTypes.hpp"
#include "AiToolRegistry.hpp"

namespace luma::ai::automation {

class AiWorkflowEngine {
public:
    explicit AiWorkflowEngine(AiToolRegistry& registry);

    WorkflowExecution Execute(const WorkflowDefinition& workflow,
                               const std::string& execution_id = {});

private:
    AiToolRegistry& registry_;
};

} // namespace luma::ai::automation
