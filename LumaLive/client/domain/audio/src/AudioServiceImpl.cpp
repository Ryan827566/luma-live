#include "../include/IAudioService.hpp"
namespace luma::client::domain::audio {
class AudioServiceImpl final : public IAudioService {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
