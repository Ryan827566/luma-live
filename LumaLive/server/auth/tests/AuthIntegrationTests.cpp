#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "IAccountService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

namespace {

std::string ExtractAfter(const std::string& text,const std::string& prefix,const std::string& terminator=""){
    const auto pos=text.find(prefix);
    assert(pos!=std::string::npos);
    const auto begin=pos+prefix.size();
    if(terminator.empty())return text.substr(begin);
    const auto end=text.find(terminator,begin);
    assert(end!=std::string::npos);
    return text.substr(begin,end-begin);
}

}

int main() {
    const std::string abc="abc";
    const auto sha=luma::contracts::auth::crypto::sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(abc.data()),abc.size()));
    assert(luma::contracts::auth::crypto::hex(sha)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::string salt="salt";
    const auto dk=luma::contracts::auth::crypto::pbkdf2_hmac_sha256(
        "password",std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(salt.data()),salt.size()),1);
    assert(luma::contracts::auth::crypto::hex(dk)=="120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");

    auto server=luma::server::auth::CreateAuthService(
        luma::server::auth::CreateInMemoryAuthStore());
    assert(server->StartOnPort(19121).IsOk());

#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "IAccountService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

namespace {

std::string ExtractAfter(const std::string& text,const std::string& prefix,const std::string& terminator=""){
    const auto pos=text.find(prefix);
    assert(pos!=std::string::npos);
    const auto begin=pos+prefix.size();
    if(terminator.empty())return text.substr(begin);
    const auto end=text.find(terminator,begin);
    assert(end!=std::string::npos);
    return text.substr(begin,end-begin);
}

}

int main() {
    const std::string abc="abc";
    const auto sha=luma::contracts::auth::crypto::sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(abc.data()),abc.size()));
    assert(luma::contracts::auth::crypto::hex(sha)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::string salt="salt";
    const auto dk=luma::contracts::auth::crypto::pbkdf2_hmac_sha256(
        "password",std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(salt.data()),salt.size()),1);
    assert(luma::contracts::auth::crypto::hex(dk)=="120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");

    auto server=luma::server::auth::CreateAuthService(
        luma::server::auth::CreateInMemoryAuthStore());
    assert(server->StartOnPort(19121).IsOk());

    auto alice=luma::client::account::CreateAccountService();
    assert(alice->Connect("127.0.0.1",19121).success);
    assert(alice->Register("alice.test","alice@example.com","Alice Test","correct horse").success);
    assert(alice->Login("unknown-user-should-not-enumerate","correct horse").success==false);
    assert(alice->Login("alice.test","wrong pass").success==false);
    assert(alice->Login("alice@example.com","correct horse","alice-pc","Alice PC").success);
    assert(alice->IsAuthenticated());

    assert(alice->GetSecuritySummary().success);
    auto security=alice->Security();
    assert(!security.email_verified);
    assert(!security.mfa_enabled);
    assert(security.active_session_count==1);

    auto verification=alice->RequestEmailVerification();
    assert(verification.success);
    const auto verification_token=ExtractAfter(verification.message,"email verification token="," expires=");
    assert(!verification_token.empty());
    assert(alice->VerifyEmail(verification_token).success);
    assert(alice->GetSecuritySummary().success);
    assert(alice->Security().email_verified);

    const std::string phone="+14155552673";
    const auto phoneVerification=alice->RequestPhoneVerification(phone);
    assert(phoneVerification.success);
    const auto phoneVerificationChallenge=ExtractAfter(
        phoneVerification.message,"challenge="," expires=");
    const auto phoneVerificationCode=ExtractAfter(
        phoneVerification.message,"SMS verification code="," challenge=");
    assert(!phoneVerificationChallenge.empty()&&!phoneVerificationCode.empty());
    assert(alice->VerifyPhone(phoneVerificationChallenge,phoneVerificationCode).success);
    assert(alice->GetProfile().success);
    assert(alice->Session().user.phone==phone);
    assert(alice->GetSecuritySummary().success);
    assert(alice->Security().phone_verified);

    auto alice2=luma::client::account::CreateAccountService();
    assert(alice2->Connect("127.0.0.1",19121).success);
    assert(alice2->Login("alice.test","correct horse","alice-phone","Alice Phone").success);
    assert(alice2->IsAuthenticated());

    assert(alice->GetSessions().success);
    assert(alice->Sessions().size()==2);
    std::string other_session_id;
    for(const auto&session:alice->Sessions()){
        if(!session.current)other_session_id=session.session_id;
    }
    assert(!other_session_id.empty());
    assert(alice->RevokeSession(other_session_id).success);
    assert(!alice2->ValidateSession().success);

    assert(alice->Login("alice@example.com","correct horse","alice-phone-2","Alice Phone 2").success);
    assert(alice->RevokeOtherSessions().success);
    assert(!alice2->IsAuthenticated() || !alice2->ValidateSession().success);

    assert(alice->GetSecurityEvents().success);
    bool sawLogin=false,sawNewNetwork=false,sawVerify=false,sawRevoke=false;
    for(const auto&event:alice->SecurityEvents()){
        sawLogin|=event.type=="login_success";
        sawNewNetwork|=event.type=="new_network";
        sawVerify|=event.type=="email_verified";
        sawRevoke|=event.type=="session_revoked";
    }
    assert(sawLogin&&sawNewNetwork&&sawVerify&&sawRevoke);

    const auto mfa=alice->EnableMfa();
    assert(mfa.success);
    const auto recovery_code=ExtractAfter(mfa.message,"MFA enabled; recovery code=");
    assert(recovery_code.size()==32);
    assert(alice->Security().mfa_enabled);

    assert(alice->Logout().success);

    auto alicePhone=luma::client::account::CreateAccountService();
    assert(alicePhone->Connect("127.0.0.1",19121).success);
    const auto phoneCodeRequest=alicePhone->RequestPhoneLoginCode(phone);
    assert(phoneCodeRequest.success);
    const auto phoneLoginChallenge=ExtractAfter(
        phoneCodeRequest.message,"challenge="," expires=");
    const auto phoneLoginCode=ExtractAfter(
        phoneCodeRequest.message,"SMS login code="," challenge=");
    assert(!phoneLoginChallenge.empty()&&!phoneLoginCode.empty());
    assert(alicePhone->LoginWithPhoneCode(
        phoneLoginChallenge,phoneLoginCode,"alice-phone","Alice Phone",recovery_code).success);
    assert(alicePhone->GetProfile().success);
    assert(alicePhone->Session().user.phone==phone);
    assert(alicePhone->GetSecuritySummary().success);
    assert(alicePhone->Security().phone_verified);
    assert(alicePhone->Logout().success);
    assert(alicePhone->LoginWithPhoneCode(
        phoneLoginChallenge,phoneLoginCode,"alice-phone","Alice Phone",recovery_code).success==false);
    alicePhone->Stop();

    assert(!alice->Login("alice.test","correct horse","alice-pc","Alice PC").success);
    assert(alice->Login("alice.test","correct horse","alice-pc","Alice PC",recovery_code).success);
    assert(alice->Logout().success);
    assert(alice->Login("alice.test","correct horse","alice-pc","Alice PC",recovery_code).success);
    assert(alice->DisableMfa(recovery_code).success);
    assert(!alice->Security().mfa_enabled);

    assert(alice->Logout().success);
    assert(alice->Login("alice.test","correct horse","alice-pc","Alice PC").success);

    assert(alice->ChangePassword("wrong password","new correct horse").success==false);
    assert(alice->ChangePassword("correct horse","new correct horse").success);
    assert(!alice->IsAuthenticated());
    assert(!alice->ValidateSession().success);
    assert(!alice2->ValidateSession().success);

    assert(alice->Login("alice.test","correct horse").success==false);
    assert(alice->Login("alice.test","new correct horse","alice-pc","Alice PC").success);

    auto bob=luma::client::account::CreateAccountService();
    assert(bob->Connect("127.0.0.1",19121).success);
    assert(bob->Register("bob.test","bob@example.com","Bob","correct horse").success);
    assert(bob->Login("bob.test","correct horse","bob-pc","Bob PC").success);

    auto bobReset=luma::client::account::CreateAccountService();
    assert(bobReset->Connect("127.0.0.1",19121).success);
    const auto resetRequest=bobReset->RequestPasswordReset("bob.test");
    assert(resetRequest.success);
    const auto reset_token=ExtractAfter(resetRequest.message,"password reset token="," expires=");
    assert(!reset_token.empty());
    assert(bobReset->ResetPassword(reset_token,"reset correct horse").success);
    assert(!bob->ValidateSession().success);

    auto bobLogin=luma::client::account::CreateAccountService();
    assert(bobLogin->Connect("127.0.0.1",19121).success);
    assert(bobLogin->Login("bob.test","reset correct horse").success);

    auto locked=luma::client::account::CreateAccountService();
    assert(locked->Connect("127.0.0.1",19121).success);
    for(int i=0;i<4;++i)assert(!locked->Login("bob.test","bad password","lock-test","Lock Test").success);
    assert(!locked->Login("bob@example.com","bad password","lock-test","Lock Test").success);
    assert(!locked->Login("bob.test","reset correct horse","lock-test","Lock Test").success);

    assert(bobLogin->GetSecuritySummary().success);
    assert(bobLogin->GetSecurityEvents().success);
    bool sawReset=false,sawPasswordReset=false;
    for(const auto&event:bobLogin->SecurityEvents()){
        sawReset|=event.type=="password_reset_requested";
        sawPasswordReset|=event.type=="password_reset_completed";
    }
    assert(sawReset&&sawPasswordReset);

    bobLogin->Stop();
    locked->Stop();
    bobReset->Stop();
    bob->Stop();
    server->Stop();
    alice2->Stop();
    alice->Stop();
    assert(server->Stop().IsOk());

    auto server2=luma::server::auth::CreateAuthService();
    assert(server2->ConfigureStore(store).IsOk());
    assert(server2->StartOnPort(19121).IsOk());

    auto persistence=luma::client::account::CreateAccountService();
    assert(persistence->Connect("127.0.0.1",19121).success);
    assert(persistence->Login("bob.test","reset correct horse").success);
    assert(persistence->Login("legacy.test","legacy pass").success);

    persistence->Stop();
    server2->Stop();

    for(const auto& suffix:{"",".tmp",".bak",".security.log"}){
        std::filesystem::remove(store+suffix,ec);
    }

    std::cout<<"PASS: email verification, MFA recovery-code login, non-enumerating login challenge, source-scoped account rate limiting, password change, password reset, session management, security audit events and persistence\n";
    return 0;
}
