#pragma once
#include "AiTypes.hpp"
#include "ProviderRegistry.hpp"
#include <memory>
#include <string>
namespace luma::ai::model_router {
class IModelRouter{public:virtual~IModelRouter()=default;virtual std::shared_ptr<providers::IAiProvider> Route(core::Capability,const std::string& preferred={})const=0;};
class ModelRouter final:public IModelRouter{std::shared_ptr<providers::ProviderRegistry> registry_;public:explicit ModelRouter(std::shared_ptr<providers::ProviderRegistry> r):registry_(std::move(r)){}std::shared_ptr<providers::IAiProvider> Route(core::Capability c,const std::string&p={})const override{return registry_?registry_->Find(c,p):nullptr;}};
}
