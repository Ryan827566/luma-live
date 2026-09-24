#include "ISmsProvider.hpp"
#include <cstdlib>
#include <string>

namespace luma::server::auth {

namespace {

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
    const char* raw = std::getenv("LUMALIVE_SMS_PROVIDER");
    const std::string provider = raw ? raw : "";
    if (provider.empty() || provider == "mock" || provider == "development") {
        return CreateDevelopmentSmsProvider();
    }
    return std::make_unique<UnavailableSmsProvider>(provider);
}

}
