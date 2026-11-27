#pragma once
#include <memory>
#include <string>
#include <vector>
#include "IAiTool.hpp"

namespace luma::ai::automation {

class AiToolRegistry {
public:
    bool Register(std::shared_ptr<IAiTool> tool);
    bool Remove(const std::string& name);
    std::shared_ptr<IAiTool> Find(const std::string& name) const;
    std::vector<std::string> List() const;

private:
    std::vector<std::shared_ptr<IAiTool>> tools_;
};

} // namespace luma::ai::automation
