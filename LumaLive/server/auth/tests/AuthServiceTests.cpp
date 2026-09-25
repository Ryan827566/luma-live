// Regression round 3 trigger; no functional change.
// Regression round 2 trigger; no functional change.
#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "ISmsProvider.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

int main() {
    const std::string message="abc";
    const auto digest=luma::contracts::auth::crypto::sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(message.data()),message.size()));
    assert(luma::contracts::auth::crypto::hex(digest)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string totp_secret="GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    assert(luma::contracts::auth::crypto::make_totp_code(totp_secret,59)=="287082");
    assert(luma::contracts::auth::crypto::make_totp_code(totp_secret,1111111109)=="081804");
    assert(luma::contracts::auth::crypto::verify_totp(totp_secret,"287082",59));
    assert(!luma::contracts::auth::crypto::verify_totp(totp_secret,"287083",59));

#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","production");
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","");
    _putenv_s("LUMALIVE_SMS_PROVIDER","");
#else
    setenv("LUMALIVE_AUTH_ENV","production",1);
    setenv("LUMALIVE_EMAIL_PROVIDER","",1);
    setenv("LUMALIVE_SMS_PROVIDER","",1);
#endif
    auto production_guard=luma::server::auth::CreateAuthService(
        luma::server::auth::CreateInMemoryAuthStore());
    assert(production_guard && !production_guard->IsRunning());
    assert(!production_guard->StartOnPort(19131).IsOk());

#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","test");
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","development");
    _putenv_s("LUMALIVE_SMS_PROVIDER","development");
#else
    setenv("LUMALIVE_AUTH_ENV","test",1);
    setenv("LUMALIVE_EMAIL_PROVIDER","development",1);
    setenv("LUMALIVE_SMS_PROVIDER","development",1);
#endif
    auto service=luma::server::auth::CreateAuthService(
        luma::server::auth::CreateInMemoryAuthStore());
    assert(service && !service->IsRunning());
    assert(service->StartOnPort(19131).IsOk());
    assert(service->IsRunning());
    assert(service->Stop().IsOk());
    return 0;
}
