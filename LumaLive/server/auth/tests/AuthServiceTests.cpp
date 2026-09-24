#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "ISmsProvider.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include <cassert>
#include <cstdint>
#include <span>
#include <string>

int main() {
    const std::string message="abc";
    const auto digest=luma::contracts::auth::crypto::sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(message.data()),message.size()));
    assert(luma::contracts::auth::crypto::hex(digest)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string totp_secret="JBSWY3DPEHPK3PXP";
    assert(luma::contracts::auth::crypto::make_totp_code(totp_secret,59)=="287082");
    assert(luma::contracts::auth::crypto::make_totp_code(totp_secret,1111111109)=="081804");
    assert(luma::contracts::auth::crypto::verify_totp(totp_secret,"287082",59));
    assert(!luma::contracts::auth::crypto::verify_totp(totp_secret,"287083",59));

    auto service=luma::server::auth::CreateAuthService(
        luma::server::auth::CreateInMemoryAuthStore());
    assert(service && !service->IsRunning());
    return 0;
}
