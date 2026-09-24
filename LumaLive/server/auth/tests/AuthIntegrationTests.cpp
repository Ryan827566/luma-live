#include "IAuthService.hpp"
#include "IAccountService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>

int main() {
    const std::string abc="abc";
    const auto sha=luma::contracts::auth::crypto::sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(abc.data()),abc.size()));
    assert(luma::contracts::auth::crypto::hex(sha)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string salt="salt";
    const auto dk=luma::contracts::auth::crypto::pbkdf2_hmac_sha256(
        "password",std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(salt.data()),salt.size()),1);
    assert(luma::contracts::auth::crypto::hex(dk)=="120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");

    const auto store=(std::filesystem::temp_directory_path()/"luma_auth_integration_test.db").string();
    std::error_code ec;
    std::filesystem::remove(store,ec); std::filesystem::remove(store+".tmp",ec);

    auto server=luma::server::auth::CreateAuthService();
    assert(server->ConfigureStore(store).IsOk());
    assert(server->StartOnPort(19120).IsOk());

    auto client=luma::client::account::CreateAccountService();
    assert(client->Connect("127.0.0.1",19120).success);

    auto r=client->Register("alice.test","alice@example.com","Alice Test","correct horse");
    if(!r.success){std::cerr<<"registration failed: "<<r.message<<"\n";return 1;}
    r=client->Register("alice.test","other@example.com","Other","correct horse");
    assert(!r.success);
    r=client->Login("alice.test","wrong pass");
    assert(!r.success);
    r=client->Login("alice@example.com","correct horse");
    assert(r.success && client->IsAuthenticated());

    auto session=client->Session();
    assert(!session.token.empty() && session.user.username=="alice.test");
    assert(client->ValidateSession().success && client->IsAuthenticated());
    assert(client->Logout().success && !client->IsAuthenticated());

    client->Stop(); server->Stop();

    auto server2=luma::server::auth::CreateAuthService();
    assert(server2->ConfigureStore(store).IsOk());
    assert(server2->StartOnPort(19120).IsOk());
    auto client2=luma::client::account::CreateAccountService();
    assert(client2->Connect("127.0.0.1",19120).success);
    r=client2->Login("alice.test","correct horse");
    assert(r.success && client2->IsAuthenticated());
    client2->Stop(); server2->Stop();

    std::filesystem::remove(store,ec); std::filesystem::remove(store+".tmp",ec);
    std::cout<<"PASS: registration, duplicate protection, password rejection, username/email login, session, validate, logout and persistence\n";
    return 0;
}
