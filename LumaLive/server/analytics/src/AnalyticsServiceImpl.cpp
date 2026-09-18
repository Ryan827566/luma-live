#include "../include/IAnalyticsService.hpp"
namespace luma::server::analytics {
class AnalyticsServiceImpl final : public IAnalyticsService {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
