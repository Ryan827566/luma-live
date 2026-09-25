#pragma once
#include "AiAutomationTypes.hpp"

namespace luma::ai::automation {

class IAiTool {
public:
    virtual ~IAiTool() = default;
    virtual const std::string& Name() const = 0;
    virtual bool Execute(const ToolCall& call, std::string& output) = 0;
};

} // namespace luma::ai::automation
