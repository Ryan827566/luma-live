#include "AiTriggerEngine.hpp"
namespace luma::ai::automation {
std::vector<std::string> AiTriggerEngine::Match(
    const AutomationEvent& event, const std::vector<WorkflowDefinition>& workflows) const {
    std::vector<std::string> matches;
    if (event.name.empty()) return matches;
    for (const auto& workflow : workflows) {
        if (workflow.id.empty() || workflow.trigger.type != TriggerType::Event ||
            workflow.trigger.name != event.name) continue;
        bool ok = true;
        for (const auto& [key, expected] : workflow.trigger.parameters) {
            const auto it = event.attributes.find(key);
            if (it == event.attributes.end() || it->second != expected) { ok = false; break; }
        }
        if (ok) matches.push_back(workflow.id);
    }
    return matches;
}
} // namespace luma::ai::automation
