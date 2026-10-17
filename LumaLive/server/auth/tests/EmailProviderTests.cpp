#include "IEmailProvider.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>

int main() {
    using namespace luma::server::auth;
#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","production");
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","");
#else
    setenv("LUMALIVE_AUTH_ENV","production",1);
    setenv("LUMALIVE_EMAIL_PROVIDER","",1);
#endif
    auto fail_closed=CreateEmailProviderFromEnvironment();
    const auto closed=fail_closed->SendToken("alice@example.com","password_reset","token-123",std::chrono::seconds(1800));
    assert(!closed.accepted&&closed.debug_token.empty());

#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","production");
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","unknown-provider");
#else
    setenv("LUMALIVE_AUTH_ENV","production",1);
    setenv("LUMALIVE_EMAIL_PROVIDER","unknown-provider",1);
#endif
    auto unknown=CreateEmailProviderFromEnvironment();
    const auto unknown_result=unknown->SendToken("alice@example.com","password_reset","token-123",std::chrono::seconds(1800));
    assert(!unknown_result.accepted&&unknown_result.debug_token.empty());

#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","test");
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","development");
#else
    setenv("LUMALIVE_AUTH_ENV","test",1);
    setenv("LUMALIVE_EMAIL_PROVIDER","development",1);
#endif
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

#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","production");
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","development");
    assert(!CreateEmailProviderFromEnvironment()->IsConfigured());
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","disabled");
    auto disabled=CreateEmailProviderFromEnvironment();
    assert(disabled->IsConfigured());
    const auto not_sent=disabled->SendToken("recipient","test","secret",std::chrono::seconds(60));
    assert(!not_sent.accepted&&not_sent.provider_message_id.empty());
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","webhook");
    _putenv_s("LUMALIVE_EMAIL_WEBHOOK_TOKEN","test-token");
    for(const char* invalid:{"http://example.com/delivery","https://user:password@example.com/delivery","https://example.com/delivery?token=secret","https://example.com/delivery#fragment","https://example.com/\r\n"}){
        _putenv_s("LUMALIVE_EMAIL_WEBHOOK_URL",invalid);
        assert(!CreateEmailProviderFromEnvironment()->IsConfigured());
    }
    _putenv_s("LUMALIVE_EMAIL_WEBHOOK_URL","https://example.com/delivery");
    _putenv_s("LUMALIVE_EMAIL_WEBHOOK_TOKEN","bad\r\nheader");
    assert(!CreateEmailProviderFromEnvironment()->IsConfigured());
    _putenv_s("LUMALIVE_EMAIL_WEBHOOK_TOKEN","test-token");
    auto configured=CreateEmailProviderFromEnvironment();assert(configured->IsConfigured());
    assert(!configured->SendToken("","test","secret",std::chrono::seconds(60)).accepted);
#endif
    return 0;
}
