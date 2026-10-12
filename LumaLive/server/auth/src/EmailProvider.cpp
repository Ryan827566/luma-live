#include "IEmailProvider.hpp"
#include <cstdlib>
#include <string>
#include <utility>

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

class DevelopmentEmailProvider final : public IEmailProvider {
public:
    bool IsConfigured() const override { return true; }

    EmailSendResult SendToken(
        std::string_view email,
        std::string_view purpose,
        std::string_view token,
        std::chrono::seconds ttl) override {
        if(email.empty()||purpose.empty()||token.empty()||ttl.count()<=0)
            return {false,{},{}, "invalid email delivery request"};
        return {true,"dev-"+std::string(purpose),std::string(token),
            "development email provider accepted the token"};
    }
};

class UnavailableEmailProvider final : public IEmailProvider {
public:
    explicit UnavailableEmailProvider(std::string name):name_(std::move(name)){}
    bool IsConfigured() const override { return false; }

    EmailSendResult SendToken(
        std::string_view,
        std::string_view,
        std::string_view,
        std::chrono::seconds) override {
        return {false,{},{},
            "email provider '"+name_+"' is not implemented"};
    }
private:
    std::string name_;
};

}

std::unique_ptr<IEmailProvider> CreateDevelopmentEmailProvider() {
    return std::make_unique<DevelopmentEmailProvider>();
}

std::unique_ptr<IEmailProvider> CreateEmailProviderFromEnvironment() {
    const auto provider=EnvironmentValue("LUMALIVE_EMAIL_PROVIDER");
    const auto auth_env=EnvironmentValue("LUMALIVE_AUTH_ENV");
    if(provider=="mock"||provider=="development"){
        if(auth_env=="development"||auth_env=="test")return CreateDevelopmentEmailProvider();
        return std::make_unique<UnavailableEmailProvider>("development");
    }
    if(provider.empty()&&(auth_env=="development"||auth_env=="test"))
        return CreateDevelopmentEmailProvider();
    if(provider.empty())return std::make_unique<UnavailableEmailProvider>("unset");
    return std::make_unique<UnavailableEmailProvider>(provider);
}

}
