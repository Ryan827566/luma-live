#pragma once
#include "IAiProvider.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace luma::ai::providers {
class ProviderRegistry {
public: bool Register(AiProviderPtr); bool Remove(const std::string&); AiProviderPtr Find(core::Capability,const std::string& preferred={}) const; std::vector<core::ProviderConfig> List() const;
private: mutable std::mutex mutex_; std::vector<AiProviderPtr> providers_;
};
class DeterministicProvider final: public IAiProvider {
public: explicit DeterministicProvider(core::ProviderConfig c); const core::ProviderConfig& Config() const override{return config_;} core::AiResponse Execute(const core::AiRequest&) override;
private: core::ProviderConfig config_;
};
}
