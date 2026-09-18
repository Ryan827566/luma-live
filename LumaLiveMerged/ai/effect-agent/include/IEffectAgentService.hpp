#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace luma::ai::effect::agent {

struct OperationResult { bool success{false}; std::string message; };

class IEffectAgentService {
public:
    virtual ~IEffectAgentService() = default;
    virtual OperationResult Start() = 0;
    virtual OperationResult Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual OperationResult Execute(std::string_view operation) = 0;
};

}
