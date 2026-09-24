#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace luma::server::auth {

struct EmailSendResult {
    bool accepted{false};
    std::string provider_message_id;
    std::string debug_token;
    std::string message;
};

class IEmailProvider {
public:
    virtual ~IEmailProvider() = default;

    virtual EmailSendResult SendToken(
        std::string_view email,
        std::string_view purpose,
        std::string_view token,
        std::chrono::seconds ttl) = 0;
};

std::unique_ptr<IEmailProvider> CreateDevelopmentEmailProvider();
std::unique_ptr<IEmailProvider> CreateEmailProviderFromEnvironment();

}
