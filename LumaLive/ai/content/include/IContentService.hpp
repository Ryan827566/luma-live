#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::ai::content {
class IContentService {
public:
    virtual ~IContentService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
