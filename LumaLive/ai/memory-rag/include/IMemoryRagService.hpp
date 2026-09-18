#pragma once
#include <string>
#include <string_view>
namespace luma::ai::memory::rag {
struct OperationResult { bool success{false}; std::string message; };
class IMemoryRagService {
public:
 virtual ~IMemoryRagService() = default;
 virtual OperationResult Start() = 0;
 virtual OperationResult Stop() = 0;
 virtual bool IsRunning() const = 0;
 virtual OperationResult Execute(std::string_view operation) = 0;
};
}
