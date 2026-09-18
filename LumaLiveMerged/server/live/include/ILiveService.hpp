#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::server::live {
class ILiveService {
public:
    virtual ~ILiveService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
