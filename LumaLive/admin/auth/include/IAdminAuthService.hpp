#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace luma::admin::auth {

struct OperationResult { bool success{false}; std::string message; };

class IAdminAuthService {
public:
    virtual ~IAdminAuthService() = default;
    virtual OperationResult Start() = 0;
    virtual OperationResult Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual OperationResult Execute(std::string_view operation) = 0;
};

}
