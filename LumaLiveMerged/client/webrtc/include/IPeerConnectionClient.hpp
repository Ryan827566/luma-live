#pragma once
#include <string>
#include <string_view>
namespace luma::client::webrtc {
struct OperationResult { bool success{false}; std::string message; };
class IPeerConnectionClient {
public:
 virtual ~IPeerConnectionClient() = default;
 virtual OperationResult Start() = 0;
 virtual OperationResult Stop() = 0;
 virtual bool IsRunning() const = 0;
 virtual OperationResult Execute(std::string_view operation) = 0;
};
}
