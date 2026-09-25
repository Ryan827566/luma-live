#include "ISmsProvider.hpp"
#include <cassert>
#include <chrono>
#include <string>
#include <cstdlib>

int main() {
    using namespace luma::server::auth;
#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","production");
    _putenv_s("LUMALIVE_SMS_PROVIDER","");
#else
    setenv("LUMALIVE_AUTH_ENV","production",1);
    setenv("LUMALIVE_SMS_PROVIDER","",1);
#endif
    auto fail_closed=CreateSmsProviderFromEnvironment();
    const auto closed=fail_closed->SendOtp("+14155552673","phone_login","123456",std::chrono::seconds(300));
    assert(!closed.accepted&&closed.debug_code.empty());
#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","test");
    _putenv_s("LUMALIVE_SMS_PROVIDER","development");
#else
    setenv("LUMALIVE_AUTH_ENV","test",1);
    setenv("LUMALIVE_SMS_PROVIDER","development",1);
#endif

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
