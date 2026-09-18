#include "../include/IUiService.hpp"
namespace luma::client::ui {
class UiServiceImpl final : public IUiService {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
