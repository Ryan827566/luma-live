#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::client::ui {
class IUiService {
public:
    virtual ~IUiService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
