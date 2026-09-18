#pragma once
#include "contracts/errors/Error.hpp"
namespace luma::adapters::transport {
class ITransportAdapter {
public:
    virtual ~ITransportAdapter() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
};
}
