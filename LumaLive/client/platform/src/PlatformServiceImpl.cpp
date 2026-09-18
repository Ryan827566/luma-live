#include "../include/IPlatformService.hpp"
namespace luma::client::platform {
class PlatformServiceImpl final : public IPlatformService {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
