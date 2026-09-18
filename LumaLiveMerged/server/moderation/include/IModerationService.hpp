#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::moderation {
class IModerationService {
public:
    virtual ~IModerationService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
