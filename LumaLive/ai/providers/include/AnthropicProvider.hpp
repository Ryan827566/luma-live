#pragma once
#include "IAiProvider.hpp"
#include "IAiHttpClient.hpp"
#include <memory>
namespace luma::ai::providers {
class AnthropicProvider final : public IAiProvider {
public:
 AnthropicProvider(core::ProviderConfig config,std::shared_ptr<IAiHttpClient> client);
 const core::ProviderConfig& Config() const override { return config_; }
 core::AiResponse Execute(const core::AiRequest& request) override;
private:
 core::ProviderConfig config_;
 std::shared_ptr<IAiHttpClient> client_;
};
core::ProviderConfig MakeAnthropicConfig(std::string endpoint,std::string apiKeyEnv,std::string model);
}