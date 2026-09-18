#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::domain::audio {
class IAudioService {
public:
    virtual ~IAudioService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
