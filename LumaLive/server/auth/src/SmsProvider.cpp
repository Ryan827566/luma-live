#include "ISmsProvider.hpp"
#include <cstdlib>
#include <string>

namespace luma::server::auth {

namespace {

std::string EnvironmentValue(const char* name) {
#ifdef _WIN32
    char* value=nullptr;
    std::size_t length=0;
    if(_dupenv_s(&value,&length,name)!=0||!value)return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value=std::getenv(name);
    return value?std::string(value):std::string{};
#endif
}

class DevelopmentSmsProvider final : public ISmsProvider {
public:
    SmsSendResult SendOtp(
        std::string_view phone,
        std::string_view purpose,
        std::string_view code,
        std::chrono::seconds ttl) override {
        if (phone.empty() || purpose.empty() || code.empty() || ttl.count() <= 0) {
            return {false, {}, {}, "invalid SMS delivery request"};
        }
        return {true, "dev-" + std::string(purpose), std::string(code), "development SMS provider accepted the OTP"};
    }
};

class UnavailableSmsProvider final : public ISmsProvider {
public:
    explicit UnavailableSmsProvider(std::string name) : name_(std::move(name)) {}

    SmsSendResult SendOtp(
        std::string_view,
        std::string_view,
        std::string_view,
        std::chrono::seconds) override {
        return {false, {}, {}, "SMS provider '" + name_ + "' is not implemented"};
    }

private:
    std::string name_;
};

}

std::unique_ptr<ISmsProvider> CreateDevelopmentSmsProvider() {
    return std::make_unique<DevelopmentSmsProvider>();
}

std::unique_ptr<ISmsProvider> CreateSmsProviderFromEnvironment() {
    const std::string provider=EnvironmentValue("LUMALIVE_SMS_PROVIDER");
    if(provider=="mock"||provider=="development"){
        const auto auth_env=EnvironmentValue("LUMALIVE_AUTH_ENV");
        if(auth_env=="development"||auth_env=="test")return CreateDevelopmentSmsProvider();
        return std::make_unique<UnavailableSmsProvider>("development");
    }
    if(provider.empty())return std::make_unique<UnavailableSmsProvider>("unset");
    return std::make_unique<UnavailableSmsProvider>(provider);
}

}
