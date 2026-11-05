#pragma once
#include <vector>
#include "AiAutomationTypes.hpp"

namespace luma::ai::automation {
struct AutomationEvent {
    std::string name;
    std::map<std::string, std::string> attributes;
};
class AiTriggerEngine {
public:
    std::vector<std::string> Match(const AutomationEvent& event,
                                   const std::vector<WorkflowDefinition>& workflows) const;
};
} // namespace luma::ai::automation
