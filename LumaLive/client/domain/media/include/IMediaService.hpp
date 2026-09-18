#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::domain::media {
class IMediaService {
public:
    virtual ~IMediaService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
