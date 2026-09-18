#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::media {
class IMediaService {
public:
    virtual ~IMediaService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
