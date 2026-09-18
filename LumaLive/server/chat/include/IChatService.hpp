#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::chat {
class IChatService {
public:
    virtual ~IChatService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
