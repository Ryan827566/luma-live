#include "AiToolRegistry.hpp"
#include <algorithm>

namespace luma::ai::automation {

bool AiToolRegistry::Register(std::shared_ptr<IAiTool> tool) {
    if (!tool || tool->Name().empty() || Find(tool->Name())) return false;
    tools_.push_back(std::move(tool));
    return true;
}

bool AiToolRegistry::Remove(const std::string& name) {
    const auto old_size = tools_.size();
    tools_.erase(std::remove_if(tools_.begin(), tools_.end(),
        [&](const auto& tool) { return tool && tool->Name() == name; }), tools_.end());
    return tools_.size() != old_size;
}

std::shared_ptr<IAiTool> AiToolRegistry::Find(const std::string& name) const {
    for (const auto& tool : tools_) {
        if (tool && tool->Name() == name) return tool;
    }
    return nullptr;
}

std::vector<std::string> AiToolRegistry::List() const {
    std::vector<std::string> result;
    result.reserve(tools_.size());
    for (const auto& tool : tools_) if (tool) result.push_back(tool->Name());
    return result;
}

} // namespace luma::ai::automation
