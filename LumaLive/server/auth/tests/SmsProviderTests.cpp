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
    _putenv_s("LUMALIVE_AUTH_ENV","production");
    _putenv_s("LUMALIVE_SMS_PROVIDER","unknown-provider");
#else
    setenv("LUMALIVE_AUTH_ENV","production",1);
    setenv("LUMALIVE_SMS_PROVIDER","unknown-provider",1);
#endif
    auto unknown=CreateSmsProviderFromEnvironment();
    const auto unknown_result=unknown->SendOtp("+14155552673","phone_login","123456",std::chrono::seconds(300));
    assert(!unknown_result.accepted&&unknown_result.debug_code.empty());

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


#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","production");
    _putenv_s("LUMALIVE_SMS_PROVIDER","development");
    assert(!CreateSmsProviderFromEnvironment()->IsConfigured());
    _putenv_s("LUMALIVE_SMS_PROVIDER","disabled");
    auto disabled=CreateSmsProviderFromEnvironment();
    assert(disabled->IsConfigured());
    const auto not_sent=disabled->SendOtp("recipient","test","secret",std::chrono::seconds(60));
    assert(!not_sent.accepted&&not_sent.provider_message_id.empty());
    _putenv_s("LUMALIVE_SMS_PROVIDER","webhook");
    _putenv_s("LUMALIVE_SMS_WEBHOOK_TOKEN","test-token");
    for(const char* invalid:{"http://example.com/delivery","https://user:password@example.com/delivery","https://example.com/delivery?token=secret","https://example.com/delivery#fragment","https://example.com/\r\n"}){
        _putenv_s("LUMALIVE_SMS_WEBHOOK_URL",invalid);
        assert(!CreateSmsProviderFromEnvironment()->IsConfigured());
    }
    _putenv_s("LUMALIVE_SMS_WEBHOOK_URL","https://example.com/delivery");
    _putenv_s("LUMALIVE_SMS_WEBHOOK_TOKEN","bad\r\nheader");
    assert(!CreateSmsProviderFromEnvironment()->IsConfigured());
    _putenv_s("LUMALIVE_SMS_WEBHOOK_TOKEN","test-token");
    auto configured=CreateSmsProviderFromEnvironment();assert(configured->IsConfigured());
    assert(!configured->SendOtp("","test","secret",std::chrono::seconds(60)).accepted);
#endif
    return 0;
}
