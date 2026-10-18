#include "IAuthService.hpp"
#include "IAuthStore.hpp"
#include "IAccountService.hpp"
#include "contracts/auth/AuthCrypto.hpp"
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string ExtractAfter(
    const std::string& text,
    const std::string& prefix,
    const std::string& terminator="") {
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
#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","test");
    _putenv_s("LUMALIVE_EMAIL_PROVIDER","development");
    _putenv_s("LUMALIVE_SMS_PROVIDER","development");
#else
    setenv("LUMALIVE_AUTH_ENV","test",1);
    setenv("LUMALIVE_EMAIL_PROVIDER","development",1);
    setenv("LUMALIVE_SMS_PROVIDER","development",1);
#endif
    const std::string abc="abc";
    const auto sha=luma::contracts::auth::crypto::sha256(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(abc.data()),abc.size()));
    assert(luma::contracts::auth::crypto::hex(sha)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::string salt="salt";
    const auto dk=luma::contracts::auth::crypto::pbkdf2_hmac_sha256(
        "password",
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(salt.data()),salt.size()),
        1);
    assert(luma::contracts::auth::crypto::hex(dk)=="120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");

    const char* database_url=std::getenv("LUMALIVE_AUTH_DATABASE_URL");
    const bool use_database=database_url&&*database_url;

    auto auth_store=use_database
        ? luma::server::auth::CreatePostgresAuthStore(database_url)
        : luma::server::auth::CreateInMemoryAuthStore();

    std::atomic<std::int64_t> clock_offset{0};
    auto server=luma::server::auth::CreateAuthService(std::move(auth_store),[&]{
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()+clock_offset.load();
    });
    assert(server->StartOnPort(19121).IsOk());

    auto alice=luma::client::account::CreateAccountService();
    assert(alice->Connect("127.0.0.1",19121).success);
    assert(alice->Register(
        "alice.test","alice@example.com","Alice Test","correct horse").success);
    assert(alice->Login("unknown-user-should-not-enumerate","correct horse").success==false);
    assert(alice->Login("alice.test","wrong pass").success==false);
    assert(alice->Login(
        "alice@example.com","correct horse","alice-pc","Alice PC").success);
    assert(alice->IsAuthenticated());

    assert(alice->GetSecuritySummary().success);
    const auto security=alice->Security();
    assert(!security.email_verified);
    assert(!security.mfa_enabled);
    assert(security.active_session_count==1);

    const auto verification=alice->RequestEmailVerification();
    assert(verification.success);
    const auto verification_token=ExtractAfter(
        verification.message,"email verification token="," expires=");
    assert(!verification_token.empty());
    assert(alice->VerifyEmail(verification_token).success);
    assert(alice->GetSecuritySummary().success);
    assert(alice->Security().email_verified);

    const std::string phone="+14155552673";
    const auto phoneVerification=alice->RequestPhoneVerification(phone);
    assert(phoneVerification.success);
    const auto throttledPhoneVerification=alice->RequestPhoneVerification(phone);
    assert(!throttledPhoneVerification.success);
    const auto phoneVerificationChallenge=ExtractAfter(
        phoneVerification.message,"challenge="," expires=");
    const auto phoneVerificationCode=ExtractAfter(
        phoneVerification.message,"SMS verification code="," challenge=");
    assert(!phoneVerificationChallenge.empty()&&!phoneVerificationCode.empty());
    assert(alice->VerifyPhone(
        phoneVerificationChallenge,phoneVerificationCode).success);
    assert(alice->GetProfile().success);
    assert(alice->Session().user.phone==phone);
    assert(alice->GetSecuritySummary().success);
    assert(alice->Security().phone_verified);

    auto alice2=luma::client::account::CreateAccountService();
    assert(alice2->Connect("127.0.0.1",19121).success);
    assert(alice2->Login(
        "alice.test","correct horse","alice-phone","Alice Phone").success);
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
    assert(!alice2->IsAuthenticated());
    assert(alice2->Session().token.empty());
    assert(alice2->Session().refresh_token.empty());

    assert(alice->Login(
        "alice@example.com","correct horse",
        "alice-phone-2","Alice Phone 2").success);
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
    const auto recovery_codes_text=ExtractAfter(
        mfa.message,"MFA enabled; recovery codes=","; TOTP secret=");
    std::vector<std::string> recovery_codes;
    {
        std::size_t start=0;
        while(start<=recovery_codes_text.size()){
            const auto end=recovery_codes_text.find(',',start);
            const auto value=recovery_codes_text.substr(
                start,end==std::string::npos?std::string::npos:end-start);
            if(!value.empty())recovery_codes.push_back(value);
            if(end==std::string::npos)break;
            start=end+1;
        }
    }
    assert(recovery_codes.size()==10);
    const auto recovery_code=recovery_codes[0];
    const auto recovery_code_2=recovery_codes[1];
    const auto recovery_code_3=recovery_codes[2];
    const auto recovery_code_4=recovery_codes[3];
    const auto recovery_code_5=recovery_codes[4];
    const auto totp_secret=ExtractAfter(
        mfa.message,"TOTP secret=","; otpauth=");
    const auto otpauth=ExtractAfter(mfa.message,"otpauth=");
    assert(recovery_code.size()==32);
    assert(recovery_code_2.size()==32&&recovery_code_3.size()==32&&recovery_code_4.size()==32);
    assert(totp_secret.size()>=16);
    assert(otpauth.rfind("otpauth://totp/",0)==0);
    assert(alice->Security().mfa_enabled);

    assert(alice->Logout().success);
    assert(alice->SecurityEvents().empty());
    assert(alice->Sessions().empty());
    assert(!alice->Security().mfa_enabled);
    assert(alice->Login(
        "alice.test","correct horse","alice-totp","Alice TOTP",
        luma::contracts::auth::crypto::make_totp_code(
            totp_secret,
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())).success);
    const auto token_before_refresh=alice->Session().token;
    const auto refresh_before=alice->Session().refresh_token;
    const auto session_id_before=alice->Session().session_id;
    assert(!token_before_refresh.empty()&&!refresh_before.empty());
    assert(alice->RefreshSession().success);
    assert(alice->Session().token!=token_before_refresh);
    assert(alice->Session().refresh_token!=refresh_before);
    assert(alice->Session().session_id==session_id_before);
    const auto refresh_after=alice->Session().refresh_token;
    assert(alice->ValidateSession().success);
    assert(alice->Session().refresh_token==refresh_after);
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
    const auto needsMfa=alicePhone->LoginWithPhoneCode(
        phoneLoginChallenge,phoneLoginCode,"alice-phone","Alice Phone","");
    assert(!needsMfa.success&&needsMfa.code=="mfa_required");
    assert(!alicePhone->IsAuthenticated()&&alicePhone->Session().token.empty());
    const auto badMfa=alicePhone->LoginWithPhoneCode(
        phoneLoginChallenge,phoneLoginCode,"alice-phone","Alice Phone","not-a-valid-mfa-code");
    assert(!badMfa.success&&badMfa.code=="invalid_mfa_code");
    assert(!alicePhone->IsAuthenticated());
    assert(alicePhone->LoginWithPhoneCode(
        phoneLoginChallenge,phoneLoginCode,
        "alice-phone","Alice Phone",recovery_code).success);
    assert(alicePhone->GetProfile().success);
    assert(alicePhone->Session().user.phone==phone);
    assert(alicePhone->GetSecuritySummary().success);
    assert(alicePhone->Security().phone_verified);
    assert(alicePhone->Logout().success);
    assert(alicePhone->LoginWithPhoneCode(
        phoneLoginChallenge,phoneLoginCode,
        "alice-phone","Alice Phone",
        luma::contracts::auth::crypto::make_totp_code(
            totp_secret,
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())).success==false);
    // Advance the injected test clock beyond the real per-number resend throttle.
    clock_offset=61;
    const auto retryRequest=alicePhone->RequestPhoneLoginCode(phone);
    assert(retryRequest.success);
    const auto retryChallenge=ExtractAfter(retryRequest.message,"challenge="," expires=");
    const auto retryCode=ExtractAfter(retryRequest.message,"SMS login code="," challenge=");
    // A missing factor does not spend the attempt budget; wrong supplied factors do.
    for(int attempt=0;attempt<6;++attempt){
        const auto prompt=alicePhone->LoginWithPhoneCode(retryChallenge,retryCode,"retry","Retry","");
        assert(!prompt.success&&prompt.code=="mfa_required");
    }
    for(int attempt=0;attempt<5;++attempt){
        const auto rejected=alicePhone->LoginWithPhoneCode(retryChallenge,retryCode,"retry","Retry","invalid-mfa");
        assert(!rejected.success&&rejected.code=="invalid_mfa_code");
    }
    const auto exhausted=alicePhone->LoginWithPhoneCode(retryChallenge,retryCode,"retry","Retry",recovery_code_2);
    assert(!exhausted.success&&exhausted.code=="invalid_token");
    assert(!alicePhone->IsAuthenticated());
    alicePhone->Stop();
    clock_offset=0;

    assert(!alice->Login(
        "alice.test","correct horse","alice-pc","Alice PC").success);
    assert(alice->Login(
        "alice.test","correct horse",
        "alice-pc","Alice PC",recovery_code).success==false);
    assert(alice->Login(
        "alice.test","correct horse",
        "alice-pc","Alice PC",recovery_code_2).success);
    assert(alice->Logout().success);
    assert(alice->Login(
        "alice.test","correct horse",
        "alice-pc","Alice PC",recovery_code_3).success);
    assert(alice->Logout().success);
    assert(alice->Login(
        "alice.test","correct horse",
        "alice-pc","Alice PC",recovery_code_4).success);
    assert(alice->DisableMfa(recovery_code_5).success);
    assert(!alice->Security().mfa_enabled);

    assert(alice->Logout().success);
    assert(alice->Login(
        "alice.test","correct horse","alice-pc","Alice PC").success);

    assert(alice->ChangePassword(
        "wrong password","new correct horse").success==false);
    assert(alice->ChangePassword(
        "correct horse","new correct horse").success);
    assert(!alice->IsAuthenticated());
    assert(!alice->ValidateSession().success);
    assert(!alice2->ValidateSession().success);

    assert(alice->Login(
        "alice.test","correct horse").success==false);
    assert(alice->Login(
        "alice.test","new correct horse","alice-pc","Alice PC").success);

    auto bob=luma::client::account::CreateAccountService();
    assert(bob->Connect("127.0.0.1",19121).success);
    assert(bob->Register(
        "bob.test","bob@example.com","Bob","correct horse").success);
    assert(bob->Login(
        "bob.test","correct horse","bob-pc","Bob PC").success);

    auto bobReset=luma::client::account::CreateAccountService();
    assert(bobReset->Connect("127.0.0.1",19121).success);
    const auto missingReset=bobReset->RequestPasswordReset("account-that-does-not-exist");
    assert(missingReset.success);
    assert(missingReset.message.find("if the account exists")!=std::string::npos);

    const auto resetRequest=bobReset->RequestPasswordReset("bob.test");
    assert(resetRequest.success);
    const auto reset_token=ExtractAfter(
        resetRequest.message,"password reset token="," expires=");
    assert(!reset_token.empty());
    // A token delivered by email remains usable after the requesting client exits.
    bobReset->Stop();
    bobReset=luma::client::account::CreateAccountService();
    assert(bobReset->Connect("127.0.0.1",19121).success);
    assert(bobReset->ResetPassword(
        reset_token,"reset correct horse").success);
    assert(!bobReset->ResetPassword(reset_token,"replayed password").success);
    assert(!bob->ValidateSession().success);
    assert(!bob->IsAuthenticated());
    assert(bob->Session().token.empty());

    auto bobLogin=luma::client::account::CreateAccountService();
    assert(bobLogin->Connect("127.0.0.1",19121).success);
    assert(bobLogin->Login(
        "bob.test","reset correct horse").success);

    auto locked=luma::client::account::CreateAccountService();
    assert(locked->Connect("127.0.0.1",19121).success);
    for(int i=0;i<4;++i){
        assert(!locked->Login(
            "bob.test","bad password","lock-test","Lock Test").success);
    }
    assert(!locked->Login(
        "bob@example.com","bad password","lock-test","Lock Test").success);
    assert(!locked->Login(
        "bob.test","reset correct horse","lock-test","Lock Test").success);

    assert(bobLogin->GetSecuritySummary().success);
    assert(bobLogin->GetSecurityEvents().success);
    bool sawReset=false,sawPasswordReset=false;
    for(const auto&event:bobLogin->SecurityEvents()){
        sawReset|=event.type=="password_reset_requested";
        sawPasswordReset|=event.type=="password_reset_completed";
    }
    assert(sawReset&&sawPasswordReset);

    // Expired access credentials still have their own independently valid refresh lifetime.
    auto timed=luma::client::account::CreateAccountService();
    assert(timed->Connect("127.0.0.1",19121).success);
    assert(timed->Register("refresh.test","refresh@example.com","Refresh Test","correct horse").success);
    assert(timed->Login("refresh.test","correct horse").success);
    const auto original_session=timed->Session();
    clock_offset=24*60*60+1;
    assert(!timed->ValidateSession().success);
    assert(!timed->IsAuthenticated());
    assert(timed->Session().refresh_token==original_session.refresh_token);
    assert(timed->RefreshSession().success);
    assert(timed->IsAuthenticated());
    assert(timed->Session().session_id==original_session.session_id);
    assert(timed->Session().token!=original_session.token);
    assert(timed->Session().refresh_token!=original_session.refresh_token);
    assert(timed->ValidateSession().success);

    // Direct refresh must work too, without an earlier validation request.
    clock_offset=2*24*60*60+2;
    assert(timed->RefreshSession().success);
    assert(timed->ValidateSession().success);
    clock_offset=31*24*60*60;
    assert(!timed->RefreshSession().success);
    assert(!timed->IsAuthenticated());
    assert(timed->Session().token.empty());
    assert(timed->Session().refresh_token.empty());
    timed->Stop();
    clock_offset=0;

    bobLogin->Stop();
    locked->Stop();
    bobReset->Stop();
    bob->Stop();
    alice2->Stop();
    alice->Stop();
    assert(server->Stop().IsOk());

    if(use_database){
        auto server2=luma::server::auth::CreateAuthService(
            luma::server::auth::CreatePostgresAuthStore(database_url));
        assert(server2->StartOnPort(19121).IsOk());

        auto persistence=luma::client::account::CreateAccountService();
        assert(persistence->Connect("127.0.0.1",19121).success);
        assert(persistence->Login(
            "bob.test","reset correct horse").success);
        assert(persistence->GetProfile().success);
        assert(persistence->Session().user.user_id.size()>0);
        persistence->Stop();

        server2->Stop();
    }

    std::cout
        <<"PASS: server-side auth store, email verification, MFA recovery-code login, "
          "non-enumerating login, source-scoped account rate limiting, password change, "
          "cross-client password reset, phone SMS login, expired-access refresh, revoked-session "
          "cache cleanup, session management and security audit events\n";
    return 0;
}
