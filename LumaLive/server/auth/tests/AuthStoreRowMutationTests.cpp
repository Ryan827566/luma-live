#include "IAuthStore.hpp"
// Regression round 3 trigger: test-only change; no functional behavior change.
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

luma::server::auth::AuthUserRecord MakeUser(const std::string& id) {
    luma::server::auth::AuthUserRecord user{};
    user.id=id;
    user.username=id+"_user";
    user.email=id+"@example.com";
    user.display_name=id;
    user.salt_hex="00112233445566778899aabbccddeeff";
    user.verifier_hex="ffeeddccbbaa99887766554433221100";
    user.password_kdf_iterations=600000;
    return user;
}

}

int main() {
    const char* database_url=std::getenv("LUMALIVE_AUTH_DATABASE_URL");
    auto left=database_url&&*database_url
        ? luma::server::auth::CreatePostgresAuthStore(database_url)
        : luma::server::auth::CreateInMemoryAuthStore();
    auto right=database_url&&*database_url
        ? luma::server::auth::CreatePostgresAuthStore(database_url)
        : luma::server::auth::CreateInMemoryAuthStore();

    assert(left->Open().IsOk());
    if(database_url&&*database_url)assert(right->Open().IsOk());

    const auto user_a=MakeUser("row_isolation_a");
    const auto user_b=MakeUser("row_isolation_b");

    assert(left->UpsertUser(user_a).IsOk());
    assert((database_url&&*database_url ? right->UpsertUser(user_b) : left->UpsertUser(user_b)).IsOk());

    std::vector<luma::server::auth::AuthUserRecord> loaded;
    assert(left->LoadUsers(loaded).IsOk());

    bool saw_a=false;
    bool saw_b=false;
    for(const auto& user:loaded) {
        saw_a|=user.id==user_a.id;
        saw_b|=user.id==user_b.id;
    }
    assert(saw_a&&saw_b);

    auto updated_a=user_a;
    updated_a.display_name="row_isolation_a_updated";
    assert(left->UpsertUser(updated_a).IsOk());

    loaded.clear();
    assert((database_url&&*database_url ? right->LoadUsers(loaded) : left->LoadUsers(loaded)).IsOk());
    saw_a=saw_b=false;
    for(const auto& user:loaded) {
        if(user.id==user_a.id) {
            saw_a=true;
            assert(user.display_name=="row_isolation_a_updated");
        }
        if(user.id==user_b.id) {
            saw_b=true;
            assert(user.display_name==user_b.display_name);
        }
    }
    assert(saw_a&&saw_b);

    assert((database_url&&*database_url ? right->DeleteUser(user_a.id) : left->DeleteUser(user_a.id)).IsOk());
    loaded.clear();
    assert(left->LoadUsers(loaded).IsOk());
    for(const auto& user:loaded) assert(user.id!=user_a.id);

    assert(left->DeleteUser(user_b.id).IsOk());
    left->Close();
    if(database_url&&*database_url)right->Close();
    return 0;
}
