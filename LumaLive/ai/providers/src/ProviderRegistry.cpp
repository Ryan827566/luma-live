#include "ProviderRegistry.hpp"
#include <algorithm>
#include <utility>
namespace luma::ai::providers {
bool ProviderRegistry::Register(AiProviderPtr p){if(!p||p->Config().name.empty())return false;std::lock_guard l(mutex_);for(auto&x:providers_)if(x->Config().name==p->Config().name)return false;providers_.push_back(std::move(p));return true;}
bool ProviderRegistry::Remove(const std::string& n){std::lock_guard l(mutex_);auto o=providers_.size();providers_.erase(std::remove_if(providers_.begin(),providers_.end(),[&](auto&p){return p->Config().name==n;}),providers_.end());return o!=providers_.size();}
AiProviderPtr ProviderRegistry::Find(core::Capability cap,const std::string& preferred)const{std::lock_guard l(mutex_);AiProviderPtr best;for(auto&p:providers_){auto&c=p->Config();if(!c.enabled||(!preferred.empty()&&c.name!=preferred))continue;if(std::find(c.capabilities.begin(),c.capabilities.end(),cap)==c.capabilities.end())continue;if(!best||c.priority<best->Config().priority)best=p;}return best;}
std::vector<core::ProviderConfig> ProviderRegistry::List()const{std::lock_guard l(mutex_);std::vector<core::ProviderConfig>r;for(auto&p:providers_)r.push_back(p->Config());return r;}
DeterministicProvider::DeterministicProvider(core::ProviderConfig c):config_(std::move(c)){}
core::AiResponse DeterministicProvider::Execute(const core::AiRequest&r){core::AiResponse o;o.request_id=r.request_id;o.provider=config_.name;o.model=r.model.empty()&&!config_.models.empty()?config_.models.front():r.model;if(!config_.enabled){o.error="provider is disabled";return o;}if(r.input.empty()){o.error="input is empty";return o;}o.success=true;o.text="["+std::string(core::ToString(r.capability))+"] "+r.input;return o;}
}