#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace luma::server::auth {

struct SmsSendResult {
    bool accepted{false};
    std::string provider_message_id;
    std::string debug_code;
    std::string message;
};

class ISmsProvider {
public:
    virtual ~ISmsProvider() = default;

    // Providers receive the already-generated OTP. The provider is responsible only
    // for delivery; authentication state remains owned by LumaLive Auth.
    virtual bool IsConfigured() const = 0;

    virtual SmsSendResult SendOtp(
        std::string_view phone,
        std::string_view purpose,
        std::string_view code,
        std::chrono::seconds ttl) = 0;
};

// Development-only provider. It never calls an external service and returns the
// OTP through debug_code so the existing local integration UI/tests can continue
// to exercise the full phone flow.
std::unique_ptr<ISmsProvider> CreateDevelopmentSmsProvider();

// Select the provider from LUMALIVE_SMS_PROVIDER.
// Empty or "mock" selects the development provider. Other values currently
// produce a provider that reports "not configured" until a real adapter is added.
std::unique_ptr<ISmsProvider> CreateSmsProviderFromEnvironment();

}
