#include "IEmailProvider.hpp"
#include <cassert>
#include <chrono>

int main() {
    using namespace luma::server::auth;
    auto provider=CreateDevelopmentEmailProvider();
    assert(provider);

    const auto result=provider->SendToken(
        "alice@example.com","password_reset","token-123",std::chrono::seconds(1800));
    assert(result.accepted);
    assert(result.provider_message_id=="dev-password_reset");
    assert(result.debug_token=="token-123");
    assert(!result.message.empty());

    const auto invalid=provider->SendToken(
        "alice@example.com","password_reset","",std::chrono::seconds(1800));
    assert(!invalid.accepted);
    return 0;
}
