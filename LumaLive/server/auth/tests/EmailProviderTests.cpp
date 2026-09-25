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
    return 0;
}
