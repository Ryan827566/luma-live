#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::analytics {
class IAnalyticsService {
public:
    virtual ~IAnalyticsService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
