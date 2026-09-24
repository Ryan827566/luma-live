#include "ISmsProvider.hpp"
#include <cassert>
#include <chrono>
#include <string>

int main() {
    using namespace luma::server::auth;

    auto provider = CreateDevelopmentSmsProvider();
    assert(provider);

    const auto result = provider->SendOtp(
        "+14155552673",
        "phone_login",
        "123456",
        std::chrono::seconds(300));

    assert(result.accepted);
    assert(result.provider_message_id == "dev-phone_login");
    assert(result.debug_code == "123456");
    assert(!result.message.empty());

    const auto invalid = provider->SendOtp(
        "+14155552673",
        "phone_login",
        "",
        std::chrono::seconds(300));
    assert(!invalid.accepted);

    return 0;
}
