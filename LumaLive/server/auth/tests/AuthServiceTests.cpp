#include "IAuthService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include <cassert>
#include <cstdint>
#include <span>

int main() {
    const std::string message="abc";
    const auto digest=luma::contracts::auth::crypto::sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(message.data()),message.size()));
    assert(luma::contracts::auth::crypto::hex(digest)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto service=luma::server::auth::CreateAuthService();
    assert(service && !service->IsRunning());
    return 0;
}
