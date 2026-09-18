#pragma once
#include <string>
#include <string_view>
namespace luma::client::alert {
struct OperationResult { bool success{false}; std::string message; };
class IAlertClient {
public:
 virtual ~IAlertClient() = default;
 virtual OperationResult Start() = 0;
 virtual OperationResult Stop() = 0;
 virtual bool IsRunning() const = 0;
 virtual OperationResult Execute(std::string_view operation) = 0;
};
}
