#pragma once
#include <string>
#include <string_view>
namespace luma::client::account {
struct OperationResult { bool success{false}; std::string message; };
class IAccountClient {
public:
 virtual ~IAccountClient() = default;
 virtual OperationResult Start() = 0;
 virtual OperationResult Stop() = 0;
 virtual bool IsRunning() const = 0;
 virtual OperationResult Execute(std::string_view operation) = 0;
};
}
