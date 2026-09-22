#include "../include/IEncoderAdapter.hpp"
namespace luma::adapters::encoder {
class EncoderAdapterImpl final : public IEncoderAdapter {
public:
    shared::contracts::Result Start() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
    shared::contracts::Result Stop() override { return shared::contracts::Result::Failure(shared::contracts::ErrorCode::NotImplemented); }
};
}
