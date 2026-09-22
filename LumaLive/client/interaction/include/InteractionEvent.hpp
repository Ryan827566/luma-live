#pragma once
#include <string_view>
namespace luma::client::interaction {
class InteractionEvent {
public:
    virtual ~InteractionEvent() = default;
};
}
