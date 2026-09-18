#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace luma::adapters::nats {

struct OperationResult { bool success{false}; std::string message; };

class INatsAdapterService {
public:
    virtual ~INatsAdapterService() = default;
    virtual OperationResult Start() = 0;
    virtual OperationResult Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual OperationResult Execute(std::string_view operation) = 0;
};

}
